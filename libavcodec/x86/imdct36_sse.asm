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

costabs:  times 4 dd  0.98480773
          times 4 dd  0.93969262
          times 4 dd  0.86602539
          times 4 dd -0.76604444
          times 4 dd -0.64278764
          times 4 dd  0.50000000
          times 4 dd -0.50000000
          times 4 dd -0.34202015
          times 4 dd -0.17364818
          times 4 dd  0.50190992
          times 4 dd  0.51763808
          times 4 dd  0.55168896
          times 4 dd  0.61038726
          times 4 dd  0.70710677
          times 4 dd  0.87172341
          times 4 dd  1.18310082
          times 4 dd  1.93185163
          times 4 dd  5.73685646

%define SBLIMIT 32
SECTION_TEXT

%macro PSHUFD 3
%if cpuflag(sse2) && notcpuflag(avx)
    pshufd %1, %2, %3
%else
    shufps %1, %2, %2, %3
%endif
%endmacro

; input  %2={x1,x2,x3,x4}, %3={y1,y2,y3,y4}
; output %1={x3,x4,y1,y2}
%macro BUILDINVHIGHLOW 3
%if cpuflag(avx)
    shufps %1, %2, %3, 0x4e
%else
    movlhps %1, %3
    movhlps %1, %2
%endif
%endmacro

; input  %2={x1,x2,x3,x4}, %3={y1,y2,y3,y4}
; output %1={x4,y1,y2,y3}
%macro ROTLEFT 3
%if cpuflag(ssse3)
    palignr  %1, %3, %2, 12
%else
    BUILDINVHIGHLOW %1, %2, %3
    shufps  %1, %1, %3, 0x99
%endif
%endmacro

%macro INVERTHL 2
%if cpuflag(sse2)
    PSHUFD  %1, %2, 0x4e
%else
    movhlps %1, %2
    movlhps %1, %2
%endif
%endmacro

%macro BUTTERF 3
    INVERTHL %2, %1
    xorps    %1, [ps_p1p1m1m1]
    addps    %1, %2
%if cpuflag(sse3)
    mulps    %1, %1, [ps_cosh_sse3 + %3]
    PSHUFD   %2, %1, 0xb1
    addsubps %1, %1, %2
%else
    mulps    %1, [ps_cosh + %3]
    PSHUFD   %2, %1, 0xb1
    xorps    %1, [ps_p1m1p1m1]
    addps    %1, %2
%endif
%endmacro

%macro STORE 4
    movhlps %2, %1
    movss   [%3       ], %1
    movss   [%3 + 2*%4], %2
    shufps  %1, %1, 0xb1
    movss   [%3 +   %4], %1
    movhlps %2, %1
    movss   [%3 + 3*%4], %2
%endmacro

%macro LOAD 4
    movlps  %1, [%3       ]
    movhps  %1, [%3 +   %4]
    movlps  %2, [%3 + 2*%4]
    movhps  %2, [%3 + 3*%4]
    shufps  %1, %2, 0x88
%endmacro

%macro LOADA64 2
%if cpuflag(avx)
   movu     %1, [%2]
%else
   movlps   %1, [%2]
   movhps   %1, [%2 + 8]
%endif
%endmacro

%macro DEFINE_IMDCT 0
cglobal imdct36_float, 4,4,9, out, buf, in, win

    ; for(i=17;i>=1;i--) in[i] += in[i-1];
    LOADA64 m0, inq
    LOADA64 m1, inq + 16

    ROTLEFT m5, m0, m1

    PSHUFD  m6, m0, 0x93
    andps   m6, m6, [ps_mask]
    addps   m0, m0, m6

    LOADA64 m2, inq + 32

    ROTLEFT m7, m1, m2

    addps   m1, m1, m5
    LOADA64 m3, inq + 48

    ROTLEFT m5, m2, m3

    xorps   m4, m4, m4
    movlps  m4, [inq+64]
    BUILDINVHIGHLOW m6, m3, m4
    shufps  m6, m6, m4, 0xa9

    addps   m4, m4, m6
    addps   m2, m2, m7
    addps   m3, m3, m5

    ; for(i=17;i>=3;i-=2) in[i] += in[i-2];
    movlhps m5, m5, m0
    andps   m5, m5, [ps_mask3]

    BUILDINVHIGHLOW m7, m0, m1
    andps   m7, m7, [ps_mask2]

    addps   m0, m0, m5

    BUILDINVHIGHLOW m6, m1, m2
    andps   m6, m6, [ps_mask2]

    addps  m1, m1, m7

    BUILDINVHIGHLOW m7, m2, m3
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

%ifdef ARCH_X86_64
    SWAP   m5, m8
%else
    subps  m5, m0, m3
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

    BUILDINVHIGHLOW m4, m2, m3
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
    movss   m4, [bufq + 4*68]
    movss   m7, [bufq + 4*64]
    unpcklps  m7, m7, m4
    mulps   m6, m6, [winq + 16*4]
    addps   m6, m6, m7
    movss   [outq + 64*SBLIMIT], m6
    shufps  m6, m6, m6, 0xb1
    movss   [outq + 68*SBLIMIT], m6

    mulps   m6, m3, [winq + 4*4]
    LOAD    m4, m7, bufq + 4*16, 16
    addps   m6, m6, m4
    STORE   m6, m7, outq + 16*SBLIMIT, 4*SBLIMIT

    shufps  m4, m0, m3, 0xb5
    mulps   m4, m4, [winq + 8*4]
    LOAD    m7, m6, bufq + 4*32, 16
    addps   m4, m4, m7
    STORE   m4, m6, outq + 32*SBLIMIT, 4*SBLIMIT

    shufps  m3, m3, m2, 0xb1
    mulps   m3, m3, [winq + 12*4]
    LOAD    m7, m6, bufq + 4*48, 16
    addps   m3, m3, m7
    STORE   m3, m7, outq + 48*SBLIMIT, 4*SBLIMIT

    mulps   m2, m2, [winq]
    LOAD    m6, m7, bufq, 16
    addps   m2, m2, m6
    STORE   m2, m7, outq, 4*SBLIMIT

    mulps    m4, m1, [winq + 20*4]
    STORE    m4, m7, bufq, 16

    mulps    m3, m5, [winq + 24*4]
    STORE    m3, m7, bufq + 4*16, 16

    shufps   m0, m0, m5, 0xb0
    mulps    m0, m0, [winq + 28*4]
    STORE    m0, m7, bufq + 4*32, 16

    shufps   m5, m5, m1, 0xb1
    mulps    m5, m5, [winq + 32*4]
    STORE    m5, m7, bufq + 4*48, 16

    shufps   m1, m1, m1, 0xb1
    mulps    m1, m1, [winq + 36*4]
    movss    [bufq + 4*64], m1
    shufps   m1, m1, 0xb1
    movss    [bufq + 4*68], m1
    RET
%endmacro

INIT_XMM sse
DEFINE_IMDCT

INIT_XMM sse2
DEFINE_IMDCT

INIT_XMM sse3
DEFINE_IMDCT

INIT_XMM ssse3
DEFINE_IMDCT

%ifdef HAVE_AVX
INIT_XMM avx
DEFINE_IMDCT
%endif

INIT_XMM sse

%ifdef ARCH_X86_64
%define SPILL SWAP
%define UNSPILL SWAP
%define SPILLED(x) m %+ x
%else
%define SPILLED(x) [tmpq+(x-8)*16 + 32*4]
%macro SPILL 2 ; xmm#, mempos
    movaps SPILLED(%2), m%1
%endmacro
%macro UNSPILL 2
    movaps m%1, SPILLED(%2)
%endmacro
%endif

%macro DEFINE_FOUR_IMDCT 0
cglobal four_imdct36_float, 5,5,8, out, buf, in, win, tmp
    movlps  m0, [inq+64]
    movhps  m0, [inq+64 +   72]
    movlps  m3, [inq+64 + 2*72]
    movhps  m3, [inq+64 + 3*72]

    shufps  m5, m0, m3, 0xdd
    shufps  m0, m0, m3, 0x88

    mova     m1, [inq+48]
    movu     m6, [inq+48 +   72]
    mova     m7, [inq+48 + 2*72]
    movu     m3, [inq+48 + 3*72]

    TRANSPOSE4x4PS 1, 6, 7, 3, 4

    addps   m4, m6, m7
    mova    [tmpq+4*28], m4

    addps    m7, m3
    addps    m6, m1
    addps    m3, m0
    addps    m0, m5
    addps    m0, m7
    addps    m7, m6
    mova    [tmpq+4*12], m7
    SPILL   3, 12

    mova     m4, [inq+32]
    movu     m5, [inq+32 +   72]
    mova     m2, [inq+32 + 2*72]
    movu     m7, [inq+32 + 3*72]

    TRANSPOSE4x4PS 4, 5, 2, 7, 3

    addps   m1, m7
    SPILL   1, 11

    addps   m3, m5, m2
    SPILL   3, 13

    addps    m7, m2
    addps    m5, m4
    addps    m6, m7
    mova    [tmpq], m6
    addps   m7, m5
    mova    [tmpq+4*16], m7

    mova    m2, [inq+16]
    movu    m7, [inq+16 +   72]
    mova    m1, [inq+16 + 2*72]
    movu    m6, [inq+16 + 3*72]

    TRANSPOSE4x4PS 2, 7, 1, 6, 3

    addps   m4, m6
    addps   m6, m1
    addps   m1, m7
    addps   m7, m2
    addps   m5, m6
    SPILL   5, 15
    addps   m6, m7
    mulps   m6, [costabs + 16*2]
    mova    [tmpq+4*8], m6
    SPILL   1, 10
    SPILL   0, 14

    mova    m1, [inq]
    movu    m6, [inq +   72]
    mova    m3, [inq + 2*72]
    movu    m5, [inq + 3*72]

    TRANSPOSE4x4PS 1, 6, 3, 5, 0

    addps    m2, m5
    addps    m5, m3
    addps    m7, m5
    addps    m3, m6
    addps    m6, m1
    SPILL    7, 8
    addps    m5, m6
    SPILL    6, 9
    addps    m6, m4, SPILLED(12)
    subps    m6, m2
    UNSPILL  7, 11
    SPILL    5, 11
    subps    m5, m1, m7
    mulps    m7, [costabs + 16*5]
    addps    m7, m1
    mulps    m0, m6, [costabs + 16*6]
    addps    m0, m5
    mova     [tmpq+4*24], m0
    addps    m6, m5
    mova     [tmpq+4*4], m6
    addps    m6, m4, m2
    mulps    m6, [costabs + 16*1]
    subps    m4, SPILLED(12)
    mulps    m4, [costabs + 16*8]
    addps    m2, SPILLED(12)
    mulps    m2, [costabs + 16*3]
    subps    m5, m7, m6
    subps    m5, m2
    addps    m6, m7
    addps    m6, m4
    addps    m7, m2
    subps    m7, m4
    mova     [tmpq+4*20], m7
    mova     m2, [tmpq+4*28]
    mova     [tmpq+4*28], m5
    UNSPILL  7, 13
    subps    m5, m7, m2
    mulps    m5, [costabs + 16*7]
    UNSPILL  1, 10
    mulps    m1, [costabs + 16*2]
    addps    m4, m3, m2
    mulps    m4, [costabs + 16*4]
    addps    m2, m7
    addps    m7, m3
    mulps    m7, [costabs]
    subps    m3, m2
    mulps    m3, [costabs + 16*2]
    addps    m2, m7, m5
    addps    m2, m1
    SPILL    2, 10
    addps    m7, m4
    subps    m7, m1
    SPILL    7, 12
    subps    m5, m4
    subps    m5, m1
    UNSPILL  0, 14
    SPILL    5, 13
    addps    m1, m0, SPILLED(15)
    subps    m1, SPILLED(8)
    mova     m4, [costabs + 16*5]
    mulps    m4, [tmpq]
    UNSPILL  2, 9
    addps    m4, m2
    subps    m2, [tmpq]
    mulps    m5, m1, [costabs + 16*6]
    addps    m5, m2
    SPILL    5, 9
    addps    m2, m1
    SPILL    2, 14
    UNSPILL  5, 15
    subps    m7, m5, m0
    addps    m5, SPILLED(8)
    mulps    m5, [costabs + 16*1]
    mulps    m7, [costabs + 16*8]
    addps    m0, SPILLED(8)
    mulps    m0, [costabs + 16*3]
    subps    m2, m4, m5
    subps    m2, m0
    SPILL    2, 15
    addps    m5, m4
    addps    m5, m7
    addps    m4, m0
    subps    m4, m7
    SPILL    4, 8
    mova     m7, [tmpq+4*16]
    mova     m2, [tmpq+4*12]
    addps    m0, m7, m2
    subps    m0, SPILLED(11)
    mulps    m0, [costabs + 16*2]
    addps    m4, m7, SPILLED(11)
    mulps    m4, [costabs]
    subps    m7, m2
    mulps    m7, [costabs + 16*7]
    addps    m2, SPILLED(11)
    mulps    m2, [costabs + 16*4]
    addps    m1, m7, [tmpq+4*8]
    addps    m1, m4
    addps    m4, m2
    subps    m4, [tmpq+4*8]
    SPILL    4, 11
    subps    m7, m2
    subps    m7, [tmpq+4*8]
    addps    m4, m6, SPILLED(10)
    subps    m6, SPILLED(10)
    addps    m2, m5, m1
    mulps    m2, [costabs + 16*9]
    subps    m5, m1
    mulps    m5, [costabs + 16*17]
    subps    m1, m4, m2
    addps    m4, m2
    mulps    m2, m1, [winq+4*36]
    addps    m2, [bufq+4*36]
    mova     [outq+1152], m2
    mulps    m1, [winq+4*32]
    addps    m1, [bufq+4*32]
    mova     [outq+1024], m1
    mulps    m1, m4, [winq+4*116]
    mova     [bufq+4*36], m1
    mulps    m4, [winq+4*112]
    mova     [bufq+4*32], m4
    addps    m2, m6, m5
    subps    m6, m5
    mulps    m1, m6, [winq+4*68]
    addps    m1, [bufq+4*68]
    mova     [outq+2176], m1
    mulps    m6, [winq]
    addps    m6, [bufq]
    mova     [outq], m6
    mulps    m1, m2, [winq+4*148]
    mova     [bufq+4*68], m1
    mulps    m2, [winq+4*80]
    mova     [bufq], m2
    addps    m5, m3, [tmpq+4*24]
    mova     m2, [tmpq+4*24]
    subps    m2, m3
    mova     m1, SPILLED(9)
    subps    m1, m0
    mulps    m1, [costabs + 16*10]
    addps    m0, SPILLED(9)
    mulps    m0, [costabs + 16*16]
    addps    m6, m5, m1
    subps    m5, m1
    mulps    m3, m5, [winq+4*40]
    addps    m3, [bufq+4*40]
    mova     [outq+1280], m3
    mulps    m5, [winq+4*28]
    addps    m5, [bufq+4*28]
    mova     [outq+896], m5
    mulps    m1, m6, [winq+4*120]
    mova     [bufq+4*40], m1
    mulps    m6, [winq+4*108]
    mova     [bufq+4*28], m6
    addps    m1, m2, m0
    subps    m2, m0
    mulps    m5, m2, [winq+4*64]
    addps    m5, [bufq+4*64]
    mova     [outq+2048], m5
    mulps    m2, [winq+4*4]
    addps    m2, [bufq+4*4]
    mova     [outq+128], m2
    mulps    m0, m1, [winq+4*144]
    mova     [bufq+4*64], m0
    mulps    m1, [winq+4*84]
    mova     [bufq+4*4], m1
    mova     m1, [tmpq+4*28]
    mova     m5, m1
    addps    m1, SPILLED(13)
    subps    m5, SPILLED(13)
    UNSPILL  3, 15
    addps    m2, m7, m3
    mulps    m2, [costabs + 16*11]
    subps    m3, m7
    mulps    m3, [costabs + 16*15]
    addps    m0, m2, m1
    subps    m1, m2
    SWAP     m0, m2
    mulps    m6, m1, [winq+4*44]
    addps    m6, [bufq+4*44]
    mova     [outq+1408], m6
    mulps    m1, [winq+4*24]
    addps    m1, [bufq+4*24]
    mova     [outq+768], m1
    mulps    m0, m2, [winq+4*124]
    mova     [bufq+4*44], m0
    mulps    m2, [winq+4*104]
    mova     [bufq+4*24], m2
    addps    m0, m5, m3
    subps    m5, m3
    mulps    m1, m5, [winq+4*60]
    addps    m1, [bufq+4*60]
    mova     [outq+1920], m1
    mulps    m5, [winq+4*8]
    addps    m5, [bufq+4*8]
    mova     [outq+256], m5
    mulps    m1, m0, [winq+4*140]
    mova     [bufq+4*60], m1
    mulps    m0, [winq+4*88]
    mova     [bufq+4*8], m0
    mova     m1, [tmpq+4*20]
    addps    m1, SPILLED(12)
    mova     m2, [tmpq+4*20]
    subps    m2, SPILLED(12)
    UNSPILL  7, 8
    subps    m0, m7, SPILLED(11)
    addps    m7, SPILLED(11)
    mulps    m4, m7, [costabs + 16*12]
    mulps    m0, [costabs + 16*14]
    addps    m5, m1, m4
    subps    m1, m4
    mulps    m7, m1, [winq+4*48]
    addps    m7, [bufq+4*48]
    mova     [outq+1536], m7
    mulps    m1, [winq+4*20]
    addps    m1, [bufq+4*20]
    mova     [outq+640], m1
    mulps    m1, m5, [winq+4*128]
    mova     [bufq+4*48], m1
    mulps    m5, [winq+4*100]
    mova     [bufq+4*20], m5
    addps    m6, m2, m0
    subps    m2, m0
    mulps    m1, m2, [winq+4*56]
    addps    m1, [bufq+4*56]
    mova     [outq+1792], m1
    mulps    m2, [winq+4*12]
    addps    m2, [bufq+4*12]
    mova     [outq+384], m2
    mulps    m0, m6, [winq+4*136]
    mova    [bufq+4*56], m0
    mulps    m6, [winq+4*92]
    mova     [bufq+4*12], m6
    UNSPILL  0, 14
    mulps    m0, [costabs + 16*13]
    mova     m3, [tmpq+4*4]
    addps    m2, m0, m3
    subps    m3, m0
    mulps    m0, m3, [winq+4*52]
    addps    m0, [bufq+4*52]
    mova     [outq+1664], m0
    mulps    m3, [winq+4*16]
    addps    m3, [bufq+4*16]
    mova     [outq+512], m3
    mulps    m0, m2, [winq+4*132]
    mova     [bufq+4*52], m0
    mulps    m2, [winq+4*96]
    mova     [bufq+4*16], m2
    RET
%endmacro

INIT_XMM sse
DEFINE_FOUR_IMDCT

%ifdef HAVE_AVX
INIT_XMM avx
DEFINE_FOUR_IMDCT
%endif
