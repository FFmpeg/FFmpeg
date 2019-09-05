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
cextern pw_32
cextern pw_1023
cextern pw_4095
pd_round_11: times 4 dd 1<<(11-1)
pd_round_12: times 4 dd 1<<(12-1)
pd_round_15: times 4 dd 1<<(15-1)
pd_round_19: times 4 dd 1<<(19-1)
pd_round_20: times 4 dd 1<<(20-1)

%macro CONST_DEC  3
const %1
times 4 dw %2, %3
%endmacro

%define W1sh2 22725 ; W1 = 90901 = 22725<<2 + 1
%define W2sh2 21407 ; W2 = 85627 = 21407<<2 - 1
%define W3sh2 19265 ; W3 = 77062 = 19265<<2 + 2
%define W4sh2 16384 ; W4 = 65535 = 16384<<2 - 1
%define W3sh2_lo 19266
%define W4sh2_lo 16383
%define W5sh2 12873 ; W5 = 51491 = 12873<<2 - 1
%define W6sh2  8867 ; W6 = 35468 =  8867<<2
%define W7sh2  4520 ; W7 = 18081 =  4520<<2 + 1

CONST_DEC  w4_plus_w2_hi,   W4sh2, +W2sh2
CONST_DEC  w4_min_w2_hi,    W4sh2, -W2sh2
CONST_DEC  w4_plus_w6_hi,   W4sh2, +W6sh2
CONST_DEC  w4_min_w6_hi,    W4sh2, -W6sh2
CONST_DEC  w1_plus_w3_hi,   W1sh2, +W3sh2
CONST_DEC  w3_min_w1_hi,    W3sh2, -W1sh2
CONST_DEC  w7_plus_w3_hi,   W7sh2, +W3sh2
CONST_DEC  w3_min_w7_hi,    W3sh2, -W7sh2
CONST_DEC  w1_plus_w5,   W1sh2, +W5sh2
CONST_DEC  w5_min_w1,    W5sh2, -W1sh2
CONST_DEC  w5_plus_w7,   W5sh2, +W7sh2
CONST_DEC  w7_min_w5,    W7sh2, -W5sh2
CONST_DEC  w4_plus_w2_lo,   W4sh2_lo, +W2sh2
CONST_DEC  w4_min_w2_lo,    W4sh2_lo, -W2sh2
CONST_DEC  w4_plus_w6_lo,   W4sh2_lo, +W6sh2
CONST_DEC  w4_min_w6_lo,    W4sh2_lo, -W6sh2
CONST_DEC  w1_plus_w3_lo,   W1sh2,    +W3sh2_lo
CONST_DEC  w3_min_w1_lo,    W3sh2_lo, -W1sh2
CONST_DEC  w7_plus_w3_lo,   W7sh2,    +W3sh2_lo
CONST_DEC  w3_min_w7_lo,    W3sh2_lo, -W7sh2

%include "libavcodec/x86/simple_idct10_template.asm"

SECTION .text

%macro STORE_HI_LO 12
    movq   %1, %9
    movq   %3, %10
    movq   %5, %11
    movq   %7, %12
    movhps %2, %9
    movhps %4, %10
    movhps %6, %11
    movhps %8, %12
%endmacro

%macro LOAD_ZXBW_8 16
    pmovzxbw %1, %9
    pmovzxbw %2, %10
    pmovzxbw %3, %11
    pmovzxbw %4, %12
    pmovzxbw %5, %13
    pmovzxbw %6, %14
    pmovzxbw %7, %15
    pmovzxbw %8, %16
%endmacro

%macro LOAD_ZXBW_4 9
    movh %1, %5
    movh %2, %6
    movh %3, %7
    movh %4, %8
    punpcklbw %1, %9
    punpcklbw %2, %9
    punpcklbw %3, %9
    punpcklbw %4, %9
%endmacro

%define PASS4ROWS(base, stride, stride3) \
    [base], [base + stride], [base + 2*stride], [base + stride3]

%macro idct_fn 0

define_constants _lo

cglobal simple_idct8, 1, 1, 16, 32, block
    IDCT_FN    "", 11, pw_32, 20, "store"
RET

cglobal simple_idct8_put, 3, 4, 16, 32, pixels, lsize, block
    IDCT_FN    "", 11, pw_32, 20
    lea       r3, [3*lsizeq]
    lea       r2, [pixelsq + r3]
    packuswb  m8, m0
    packuswb  m1, m2
    packuswb  m4, m11
    packuswb  m9, m10
    STORE_HI_LO PASS8ROWS(pixelsq, r2, lsizeq, r3), m8, m1, m4, m9
RET

cglobal simple_idct8_add, 3, 4, 16, 32, pixels, lsize, block
    IDCT_FN    "", 11, pw_32, 20
    lea r2, [3*lsizeq]
    %if cpuflag(sse4)
        lea r3, [pixelsq + r2]
        LOAD_ZXBW_8 m3, m5, m6, m7, m12, m13, m14, m15, PASS8ROWS(pixelsq, r3, lsizeq, r2)
        paddsw m8, m3
        paddsw m0, m5
        paddsw m1, m6
        paddsw m2, m7
        paddsw m4, m12
        paddsw m11, m13
        paddsw m9, m14
        paddsw m10, m15
    %else
        pxor m12, m12
        LOAD_ZXBW_4 m3, m5, m6, m7, PASS4ROWS(pixelsq, lsizeq, r2), m12
        paddsw m8, m3
        paddsw m0, m5
        paddsw m1, m6
        paddsw m2, m7
        lea r3, [pixelsq + 4*lsizeq]
        LOAD_ZXBW_4 m3, m5, m6, m7, PASS4ROWS(r3, lsizeq, r2), m12
        paddsw m4, m3
        paddsw m11, m5
        paddsw m9, m6
        paddsw m10, m7
        lea r3, [pixelsq + r2]
    %endif
    packuswb  m8, m0
    packuswb  m1, m2
    packuswb  m4, m11
    packuswb  m9, m10
    STORE_HI_LO PASS8ROWS(pixelsq, r3, lsizeq, r2), m8, m1, m4, m9
RET

define_constants _hi

cglobal simple_idct10, 1, 1, 16, block
    IDCT_FN    "", 12, "", 19, "store"
    RET

cglobal simple_idct10_put, 3, 3, 16, pixels, lsize, block
    IDCT_FN    "", 12, "", 19, "put", 0, pw_1023
    RET

cglobal simple_idct12, 1, 1, 16, block
    ; coeffs are already 15bits, adding the offset would cause
    ; overflow in the input
    IDCT_FN    "", 15, pw_2, 16, "store"
    RET

cglobal simple_idct12_put, 3, 3, 16, pixels, lsize, block
    ; range isn't known, so the C simple_idct range is used
    ; Also, using a bias on input overflows, so use the bias
    ; on output of the first butterfly instead
    IDCT_FN    "", 15, pw_2, 16, "put", 0, pw_4095
    RET
%endmacro

INIT_XMM sse2
idct_fn
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
idct_fn
%endif

%endif
