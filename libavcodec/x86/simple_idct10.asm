;******************************************************************************
;* x86-SIMD-optimized IDCT for prores
;* this is identical to "simple" IDCT written by Michael Niedermayer
;* except for the clip range
;*
;* Copyright (c) 2011 Ronald S. Bultje <rsbultje@gmail.com>
;* Copyright (c) 2015 Christophe Gisquet
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
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

%if ARCH_X86_64

SECTION_RODATA

cextern pw_2
cextern pw_16
cextern pw_1023
cextern pw_4095
pd_round_12: times 4 dd 1<<(12-1)
pd_round_15: times 4 dd 1<<(15-1)
pd_round_19: times 4 dd 1<<(19-1)

%macro CONST_DEC  3
const %1
times 4 dw %2, %3
%endmacro

%define W1sh2 22725 ; W1 = 90901 = 22725<<2 + 1
%define W2sh2 21407 ; W2 = 85627 = 21407<<2 - 1
%define W3sh2 19265 ; W3 = 77062 = 19265<<2 + 2
%define W4sh2 16384 ; W4 = 65535 = 16384<<2 - 1
%define W5sh2 12873 ; W5 = 51491 = 12873<<2 - 1
%define W6sh2  8867 ; W6 = 35468 =  8867<<2
%define W7sh2  4520 ; W7 = 18081 =  4520<<2 + 1

CONST_DEC  w4_plus_w2,   W4sh2, +W2sh2
CONST_DEC  w4_min_w2,    W4sh2, -W2sh2
CONST_DEC  w4_plus_w6,   W4sh2, +W6sh2
CONST_DEC  w4_min_w6,    W4sh2, -W6sh2
CONST_DEC  w1_plus_w3,   W1sh2, +W3sh2
CONST_DEC  w3_min_w1,    W3sh2, -W1sh2
CONST_DEC  w7_plus_w3,   W7sh2, +W3sh2
CONST_DEC  w3_min_w7,    W3sh2, -W7sh2
CONST_DEC  w1_plus_w5,   W1sh2, +W5sh2
CONST_DEC  w5_min_w1,    W5sh2, -W1sh2
CONST_DEC  w5_plus_w7,   W5sh2, +W7sh2
CONST_DEC  w7_min_w5,    W7sh2, -W5sh2

%include "libavcodec/x86/simple_idct10_template.asm"

section .text align=16

%macro idct_fn 0
cglobal simple_idct10, 1, 1, 16
    IDCT_FN    "", 12, "", 19
    RET

cglobal simple_idct10_put, 3, 3, 16
    IDCT_FN    "", 12, "", 19, 0, pw_1023
    RET

cglobal simple_idct12, 1, 1, 16
    ; coeffs are already 15bits, adding the offset would cause
    ; overflow in the input
    IDCT_FN    "", 15, pw_2, 16
    RET

cglobal simple_idct12_put, 3, 3, 16
    ; range isn't known, so the C simple_idct range is used
    ; Also, using a bias on input overflows, so use the bias
    ; on output of the first butterfly instead
    IDCT_FN    "", 15, pw_2, 16, 0, pw_4095
    RET
%endmacro

INIT_XMM sse2
idct_fn
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
idct_fn
%endif

%endif
