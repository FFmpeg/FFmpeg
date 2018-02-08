;******************************************************************************
;* x86-SIMD-optimized IDCT for prores
;* this is identical to "simple" IDCT written by Michael Niedermayer
;* except for the clip range
;*
;* Copyright (c) 2011 Ronald S. Bultje <rsbultje@gmail.com>
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

; add SECTION_RODATA and proper include before including this file!

%if ARCH_X86_64

%macro define_constants 1
    %undef w4_plus_w2
    %undef w4_min_w2
    %undef w4_plus_w6
    %undef w4_min_w6
    %undef w1_plus_w3
    %undef w3_min_w1
    %undef w7_plus_w3
    %undef w3_min_w7
    %define w4_plus_w2 w4_plus_w2%1
    %define w4_min_w2  w4_min_w2%1
    %define w4_plus_w6 w4_plus_w6%1
    %define w4_min_w6  w4_min_w6%1
    %define w1_plus_w3 w1_plus_w3%1
    %define w3_min_w1  w3_min_w1%1
    %define w7_plus_w3 w7_plus_w3%1
    %define w3_min_w7  w3_min_w7%1
%endmacro

; interleave data while maintaining source
; %1=type, %2=dstlo, %3=dsthi, %4=src, %5=interleave
%macro SBUTTERFLY3 5
    punpckl%1   m%2, m%4, m%5
    punpckh%1   m%3, m%4, m%5
%endmacro

; %1/%2=src1/dst1, %3/%4=dst2, %5/%6=src2, %7=shift
; action: %3/%4 = %1/%2 - %5/%6; %1/%2 += %5/%6
;         %1/%2/%3/%4 >>= %7; dword -> word (in %1/%3)
%macro SUMSUB_SHPK 7
    psubd       %3,  %1,  %5       ; { a0 - b0 }[0-3]
    psubd       %4,  %2,  %6       ; { a0 - b0 }[4-7]
    paddd       %1,  %5            ; { a0 + b0 }[0-3]
    paddd       %2,  %6            ; { a0 + b0 }[4-7]
    psrad       %1,  %7
    psrad       %2,  %7
    psrad       %3,  %7
    psrad       %4,  %7
    packssdw    %1,  %2            ; row[0]
    packssdw    %3,  %4            ; row[7]
%endmacro

; %1 = initial bias ("" if nop)
; %2 = number of bits to shift at the end
; %3 = qmat (for prores)
%macro IDCT_1D 2-3
    ; a0 = (W4 * row[0]) + (1 << (15 - 1));
    ; a1 = a0;
    ; a2 = a0;
    ; a3 = a0;
    ; a0 += W2 * row[2];
    ; a1 += W6 * row[2];
    ; a2 -= W6 * row[2];
    ; a3 -= W2 * row[2];
%ifstr %1
    mova        m15, [pd_round_ %+ %2]
%else
    paddw       m10, [%1]
%endif
    SBUTTERFLY3 wd,  0,  1, 10,  8 ; { row[0], row[2] }[0-3]/[4-7]
    pmaddwd     m2,  m0, [w4_plus_w6]
    pmaddwd     m3,  m1, [w4_plus_w6]
    pmaddwd     m4,  m0, [w4_min_w6]
    pmaddwd     m5,  m1, [w4_min_w6]
    pmaddwd     m6,  m0, [w4_min_w2]
    pmaddwd     m7,  m1, [w4_min_w2]
    pmaddwd     m0, [w4_plus_w2]
    pmaddwd     m1, [w4_plus_w2]
%ifstr %1
    ; Adding 1<<(%2-1) for >=15 bits values
    paddd       m2, m15
    paddd       m3, m15
    paddd       m4, m15
    paddd       m5, m15
    paddd       m6, m15
    paddd       m7, m15
    paddd       m0, m15
    paddd       m1, m15
%endif

    ; a0: -1*row[0]-1*row[2]
    ; a1: -1*row[0]
    ; a2: -1*row[0]
    ; a3: -1*row[0]+1*row[2]

    ; a0 +=   W4*row[4] + W6*row[6]; i.e. -1*row[4]
    ; a1 -=   W4*row[4] + W2*row[6]; i.e. -1*row[4]-1*row[6]
    ; a2 -=   W4*row[4] - W2*row[6]; i.e. -1*row[4]+1*row[6]
    ; a3 +=   W4*row[4] - W6*row[6]; i.e. -1*row[4]
    SBUTTERFLY3 wd,  8,  9, 13, 12 ; { row[4], row[6] }[0-3]/[4-7]
    pmaddwd     m10, m8, [w4_plus_w6]
    pmaddwd     m11, m9, [w4_plus_w6]
    paddd       m0,  m10            ; a0[0-3]
    paddd       m1,  m11            ; a0[4-7]
    pmaddwd     m10, m8, [w4_min_w6]
    pmaddwd     m11, m9, [w4_min_w6]
    paddd       m6,  m10           ; a3[0-3]
    paddd       m7,  m11           ; a3[4-7]
    pmaddwd     m10, m8, [w4_min_w2]
    pmaddwd     m11, m9, [w4_min_w2]
    pmaddwd     m8, [w4_plus_w2]
    pmaddwd     m9, [w4_plus_w2]
    psubd       m4,  m10           ; a2[0-3] intermediate
    psubd       m5,  m11           ; a2[4-7] intermediate
    psubd       m2,  m8            ; a1[0-3] intermediate
    psubd       m3,  m9            ; a1[4-7] intermediate

    ; load/store
    mova   [blockq+  0], m0
    mova   [blockq+ 32], m2
    mova   [blockq+ 64], m4
    mova   [blockq+ 96], m6
    mova        m10,[blockq+ 16]       ; { row[1] }[0-7]
    mova        m8, [blockq+ 48]       ; { row[3] }[0-7]
    mova        m13,[blockq+ 80]       ; { row[5] }[0-7]
    mova        m14,[blockq+112]       ; { row[7] }[0-7]
    mova   [blockq+ 16], m1
    mova   [blockq+ 48], m3
    mova   [blockq+ 80], m5
    mova   [blockq+112], m7
%if %0 == 3
    pmullw      m10,[%3+ 16]
    pmullw      m8, [%3+ 48]
    pmullw      m13,[%3+ 80]
    pmullw      m14,[%3+112]
%endif

    ; b0 = MUL(W1, row[1]);
    ; MAC(b0, W3, row[3]);
    ; b1 = MUL(W3, row[1]);
    ; MAC(b1, -W7, row[3]);
    ; b2 = MUL(W5, row[1]);
    ; MAC(b2, -W1, row[3]);
    ; b3 = MUL(W7, row[1]);
    ; MAC(b3, -W5, row[3]);
    SBUTTERFLY3 wd,  0,  1, 10, 8  ; { row[1], row[3] }[0-3]/[4-7]
    pmaddwd     m2,  m0, [w3_min_w7]
    pmaddwd     m3,  m1, [w3_min_w7]
    pmaddwd     m4,  m0, [w5_min_w1]
    pmaddwd     m5,  m1, [w5_min_w1]
    pmaddwd     m6,  m0, [w7_min_w5]
    pmaddwd     m7,  m1, [w7_min_w5]
    pmaddwd     m0, [w1_plus_w3]
    pmaddwd     m1, [w1_plus_w3]

    ; b0: +1*row[1]+2*row[3]
    ; b1: +2*row[1]-1*row[3]
    ; b2: -1*row[1]-1*row[3]
    ; b3: +1*row[1]+1*row[3]

    ; MAC(b0,  W5, row[5]);
    ; MAC(b0,  W7, row[7]);
    ; MAC(b1, -W1, row[5]);
    ; MAC(b1, -W5, row[7]);
    ; MAC(b2,  W7, row[5]);
    ; MAC(b2,  W3, row[7]);
    ; MAC(b3,  W3, row[5]);
    ; MAC(b3, -W1, row[7]);
    SBUTTERFLY3 wd,  8,  9, 13, 14 ; { row[5], row[7] }[0-3]/[4-7]

    ; b0: -1*row[5]+1*row[7]
    ; b1: -1*row[5]+1*row[7]
    ; b2: +1*row[5]+2*row[7]
    ; b3: +2*row[5]-1*row[7]

    pmaddwd     m10, m8, [w1_plus_w5]
    pmaddwd     m11, m9, [w1_plus_w5]
    pmaddwd     m12, m8, [w5_plus_w7]
    pmaddwd     m13, m9, [w5_plus_w7]
    psubd       m2,  m10           ; b1[0-3]
    psubd       m3,  m11           ; b1[4-7]
    paddd       m0,  m12            ; b0[0-3]
    paddd       m1,  m13            ; b0[4-7]
    pmaddwd     m12, m8, [w7_plus_w3]
    pmaddwd     m13, m9, [w7_plus_w3]
    pmaddwd     m8, [w3_min_w1]
    pmaddwd     m9, [w3_min_w1]
    paddd       m4,  m12           ; b2[0-3]
    paddd       m5,  m13           ; b2[4-7]
    paddd       m6,  m8            ; b3[0-3]
    paddd       m7,  m9            ; b3[4-7]

    ; row[0] = (a0 + b0) >> 15;
    ; row[7] = (a0 - b0) >> 15;
    ; row[1] = (a1 + b1) >> 15;
    ; row[6] = (a1 - b1) >> 15;
    ; row[2] = (a2 + b2) >> 15;
    ; row[5] = (a2 - b2) >> 15;
    ; row[3] = (a3 + b3) >> 15;
    ; row[4] = (a3 - b3) >> 15;
    mova        m8, [blockq+ 0]        ; a0[0-3]
    mova        m9, [blockq+16]        ; a0[4-7]
    SUMSUB_SHPK m8,  m9,  m10, m11, m0,  m1,  %2
    mova        m0, [blockq+32]        ; a1[0-3]
    mova        m1, [blockq+48]        ; a1[4-7]
    SUMSUB_SHPK m0,  m1,  m9,  m11, m2,  m3,  %2
    mova        m1, [blockq+64]        ; a2[0-3]
    mova        m2, [blockq+80]        ; a2[4-7]
    SUMSUB_SHPK m1,  m2,  m11, m3,  m4,  m5,  %2
    mova        m2, [blockq+96]        ; a3[0-3]
    mova        m3, [blockq+112]       ; a3[4-7]
    SUMSUB_SHPK m2,  m3,  m4,  m5,  m6,  m7,  %2
%endmacro

; void ff_prores_idct_put_10_<opt>(uint8_t *pixels, ptrdiff_t stride,
;                                  int16_t *block, const int16_t *qmat);

; %1 = row shift
; %2 = row bias macro
; %3 = column shift
; %4 = column bias macro
; %5 = final action (nothing, "store", "put", "add")
; %6 = min pixel value
; %7 = max pixel value
; %8 = qmat (for prores)

%macro IDCT_FN 4-8
    ; for (i = 0; i < 8; i++)
    ;     idctRowCondDC(block + i*8);
    mova        m10,[blockq+ 0]        ; { row[0] }[0-7]
    mova        m8, [blockq+32]        ; { row[2] }[0-7]
    mova        m13,[blockq+64]        ; { row[4] }[0-7]
    mova        m12,[blockq+96]        ; { row[6] }[0-7]

%if %0 == 8
    pmullw      m10,[%8+ 0]
    pmullw      m8, [%8+32]
    pmullw      m13,[%8+64]
    pmullw      m12,[%8+96]

    IDCT_1D     %1, %2, %8
%elif %2 == 11
    ; This copies the DC-only shortcut.  When there is only a DC coefficient the
    ; C shifts the value and splats it to all coeffs rather than multiplying and
    ; doing the full IDCT.  This causes a difference on 8-bit because the
    ; coefficient is 16383 rather than 16384 (which you can get with shifting).
    por      m1,  m8, m13
    por      m1,  m12
    por      m1, [blockq+ 16]       ; { row[1] }[0-7]
    por      m1, [blockq+ 48]       ; { row[3] }[0-7]
    por      m1, [blockq+ 80]       ; { row[5] }[0-7]
    por      m1, [blockq+112]       ; { row[7] }[0-7]
    pxor     m2,  m2
    pcmpeqw  m1,  m2
    psllw    m2,  m10, 3
    pand     m2,  m1
    pcmpeqb  m3,  m3
    pxor     m1,  m3
    mova    [rsp],    m1
    mova    [rsp+16], m2

    IDCT_1D  %1,  %2

    mova     m5, [rsp]
    mova     m6, [rsp+16]
    pand     m8,  m5
    por      m8,  m6
    pand     m0,  m5
    por      m0,  m6
    pand     m1,  m5
    por      m1,  m6
    pand     m2,  m5
    por      m2,  m6
    pand     m4,  m5
    por      m4,  m6
    pand     m11, m5
    por      m11, m6
    pand     m9,  m5
    por      m9,  m6
    pand     m10, m5
    por      m10, m6
%else
    IDCT_1D     %1, %2
%endif

    ; transpose for second part of IDCT
    TRANSPOSE8x8W 8, 0, 1, 2, 4, 11, 9, 10, 3
    mova   [blockq+ 16], m0
    mova   [blockq+ 48], m2
    mova   [blockq+ 80], m11
    mova   [blockq+112], m10
    SWAP         8,  10
    SWAP         1,   8
    SWAP         4,  13
    SWAP         9,  12

    ; for (i = 0; i < 8; i++)
    ;     idctSparseColAdd(dest + i, line_size, block + i);
    IDCT_1D     %3, %4

    ; clip/store
%if %0 >= 5
%ifidn %5,"store"
    ; No clamping, means pure idct
    mova  [blockq+  0], m8
    mova  [blockq+ 16], m0
    mova  [blockq+ 32], m1
    mova  [blockq+ 48], m2
    mova  [blockq+ 64], m4
    mova  [blockq+ 80], m11
    mova  [blockq+ 96], m9
    mova  [blockq+112], m10
%elifidn %5,"put"
%ifidn %6, 0
    pxor        m3, m3
%else
    mova        m3, [%6]
%endif ; ifidn %6, 0
    mova        m5, [%7]
    pmaxsw      m8,  m3
    pmaxsw      m0,  m3
    pmaxsw      m1,  m3
    pmaxsw      m2,  m3
    pmaxsw      m4,  m3
    pmaxsw      m11, m3
    pmaxsw      m9,  m3
    pmaxsw      m10, m3
    pminsw      m8,  m5
    pminsw      m0,  m5
    pminsw      m1,  m5
    pminsw      m2,  m5
    pminsw      m4,  m5
    pminsw      m11, m5
    pminsw      m9,  m5
    pminsw      m10, m5

    lea         r2, [r1*3]
    mova  [r0     ], m8
    mova  [r0+r1  ], m0
    mova  [r0+r1*2], m1
    mova  [r0+r2  ], m2
    lea         r0, [r0+r1*4]
    mova  [r0     ], m4
    mova  [r0+r1  ], m11
    mova  [r0+r1*2], m9
    mova  [r0+r2  ], m10
%endif ; %5 action
%endif; if %0 >= 5
%endmacro

%endif
