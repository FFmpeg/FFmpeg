;******************************************************************************
;* VP9 IDCT SIMD optimizations
;*
;* Copyright (C) 2013 Clément Bœsch <u pkh me>
;* Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
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

pw_11585x2: times 8 dw 23170

%macro VP9_IDCT_COEFFS 2
pw_m%1_%2:  times 4 dw -%1,  %2
pw_%2_%1:   times 4 dw  %2,  %1
pw_m%2_m%1: times 4 dw -%2, -%1
%endmacro

%macro VP9_IDCT_COEFFS_ALL 2
pw_%1x2: times 8 dw %1*2
pw_m%1x2: times 8 dw -%1*2
pw_%2x2: times 8 dw %2*2
pw_m%2x2: times 8 dw -%2*2
VP9_IDCT_COEFFS %1, %2
%endmacro

VP9_IDCT_COEFFS_ALL 15137,  6270
VP9_IDCT_COEFFS_ALL 16069,  3196
VP9_IDCT_COEFFS_ALL  9102, 13623
VP9_IDCT_COEFFS_ALL 16305,  1606
VP9_IDCT_COEFFS_ALL 10394, 12665
VP9_IDCT_COEFFS_ALL 14449,  7723
VP9_IDCT_COEFFS_ALL  4756, 15679

pd_8192: times 4 dd 8192
pw_2048: times 8 dw 2048
pw_1024: times 8 dw 1024
pw_512:  times 8 dw 512

SECTION .text

; (a*x + b*y + round) >> shift
%macro VP9_MULSUB_2W_2X 6 ; dst1, dst2, src (unchanged), round, coefs1, coefs2
    pmaddwd            m%1, m%3, %5
    pmaddwd            m%2, m%3, %6
    paddd              m%1,  %4
    paddd              m%2,  %4
    psrad              m%1,  14
    psrad              m%2,  14
%endmacro

%macro VP9_UNPACK_MULSUB_2W_4X 7 ; dst1, dst2, coef1, coef2, rnd, tmp1, tmp2
    punpckhwd          m%6, m%2, m%1
    VP9_MULSUB_2W_2X    %7,  %6,  %6, %5, [pw_m%3_%4], [pw_%4_%3]
    punpcklwd          m%2, m%1
    VP9_MULSUB_2W_2X    %1,  %2,  %2, %5, [pw_m%3_%4], [pw_%4_%3]
    packssdw           m%1, m%7
    packssdw           m%2, m%6
%endmacro

%macro VP9_STORE_2X 5 ; reg1, reg2, tmp1, tmp2, zero
    movh               m%3, [dstq]
    movh               m%4, [dstq+strideq]
    punpcklbw          m%3, m%5
    punpcklbw          m%4, m%5
    paddw              m%3, m%1
    paddw              m%4, m%2
    packuswb           m%3, m%5
    packuswb           m%4, m%5
    movh            [dstq], m%3
    movh    [dstq+strideq], m%4
%endmacro

;-------------------------------------------------------------------------------------------
; void vp9_idct_idct_4x4_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;-------------------------------------------------------------------------------------------

%macro VP9_IDCT4_1D_FINALIZE 0
    SUMSUB_BA            w, 3, 2, 4                         ; m3=t3+t0, m2=-t3+t0
    SUMSUB_BA            w, 1, 0, 4                         ; m1=t2+t1, m0=-t2+t1
    SWAP                 0, 3, 2                            ; 3102 -> 0123
%endmacro

%macro VP9_IDCT4_1D 0
    SUMSUB_BA            w, 2, 0, 4                         ; m2=IN(0)+IN(2) m0=IN(0)-IN(2)
    pmulhrsw            m2, m6                              ; m2=t0
    pmulhrsw            m0, m6                              ; m0=t1
    VP9_UNPACK_MULSUB_2W_4X 1, 3, 15137, 6270, m7, 4, 5     ; m1=t2, m3=t3
    VP9_IDCT4_1D_FINALIZE
%endmacro

; 2x2 top left corner
%macro VP9_IDCT4_2x2_1D 0
    pmulhrsw            m0, m5                              ; m0=t1
    mova                m2, m0                              ; m2=t0
    mova                m3, m1
    pmulhrsw            m1, m6                              ; m1=t2
    pmulhrsw            m3, m7                              ; m3=t3
    VP9_IDCT4_1D_FINALIZE
%endmacro

%macro VP9_IDCT4_WRITEOUT 0
    mova                m5, [pw_2048]
    pmulhrsw            m0, m5              ; (x*2048 + (1<<14))>>15 <=> (x+8)>>4
    pmulhrsw            m1, m5
    VP9_STORE_2X         0,  1,  6,  7,  4
    lea               dstq, [dstq+2*strideq]
    pmulhrsw            m2, m5
    pmulhrsw            m3, m5
    VP9_STORE_2X         2,  3,  6,  7,  4
%endmacro

INIT_MMX ssse3
cglobal vp9_idct_idct_4x4_add, 4,4,0, dst, stride, block, eob

    cmp eobd, 4 ; 2x2 or smaller
    jg .idctfull

    cmp eobd, 1 ; faster path for when only DC is set
    jne .idct2x2

    movd                m0, [blockq]
    mova                m5, [pw_11585x2]
    pmulhrsw            m0, m5
    pmulhrsw            m0, m5
    pshufw              m0, m0, 0
    pxor                m4, m4
    movh          [blockq], m4
    pmulhrsw            m0, [pw_2048]       ; (x*2048 + (1<<14))>>15 <=> (x+8)>>4
    VP9_STORE_2X         0,  0,  6,  7,  4
    lea               dstq, [dstq+2*strideq]
    VP9_STORE_2X         0,  0,  6,  7,  4
    RET

; faster path for when only top left 2x2 block is set
.idct2x2:
    movd                m0, [blockq+0]
    movd                m1, [blockq+8]
    mova                m5, [pw_11585x2]
    mova                m6, [pw_6270x2]
    mova                m7, [pw_15137x2]
    VP9_IDCT4_2x2_1D
    TRANSPOSE4x4W  0, 1, 2, 3, 4
    VP9_IDCT4_2x2_1D
    pxor                m4, m4  ; used for the block reset, and VP9_STORE_2X
    movh       [blockq+ 0], m4
    movh       [blockq+ 8], m4
    VP9_IDCT4_WRITEOUT
    RET

.idctfull: ; generic full 4x4 idct/idct
    mova                m0, [blockq+ 0]
    mova                m1, [blockq+ 8]
    mova                m2, [blockq+16]
    mova                m3, [blockq+24]
    mova                m6, [pw_11585x2]
    mova                m7, [pd_8192]       ; rounding
    VP9_IDCT4_1D
    TRANSPOSE4x4W  0, 1, 2, 3, 4
    VP9_IDCT4_1D
    pxor                m4, m4  ; used for the block reset, and VP9_STORE_2X
    mova       [blockq+ 0], m4
    mova       [blockq+ 8], m4
    mova       [blockq+16], m4
    mova       [blockq+24], m4
    VP9_IDCT4_WRITEOUT
    RET

%if ARCH_X86_64 ; TODO: 32-bit? (32-bit limited to 8 xmm reg, we use more)

;-------------------------------------------------------------------------------------------
; void vp9_idct_idct_8x8_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;-------------------------------------------------------------------------------------------

%macro VP9_IDCT8_1D_FINALIZE 0
    SUMSUB_BA            w,  3, 10, 4                       ;  m3=t0+t7, m10=t0-t7
    SUMSUB_BA            w,  1,  2, 4                       ;  m1=t1+t6,  m2=t1-t6
    SUMSUB_BA            w, 11,  0, 4                       ; m11=t2+t5,  m0=t2-t5
    SUMSUB_BA            w,  9,  8, 4                       ;  m9=t3+t4,  m8=t3-t4
    SWAP                11, 10, 2
    SWAP                 3,  9, 0
%endmacro

%macro VP9_IDCT8_1D 0
    SUMSUB_BA            w, 8, 0, 4                         ; m8=IN(0)+IN(4) m0=IN(0)-IN(4)
    pmulhrsw            m8, m12                             ; m8=t0a
    pmulhrsw            m0, m12                             ; m0=t1a
    VP9_UNPACK_MULSUB_2W_4X 2, 10, 15137,  6270, m7, 4, 5   ; m2=t2a, m10=t3a
    VP9_UNPACK_MULSUB_2W_4X 1, 11, 16069,  3196, m7, 4, 5   ; m1=t4a, m11=t7a
    VP9_UNPACK_MULSUB_2W_4X 9,  3,  9102, 13623, m7, 4, 5   ; m9=t5a,  m3=t6a
    SUMSUB_BA            w, 10,  8, 4                       ; m10=t0a+t3a (t0),  m8=t0a-t3a (t3)
    SUMSUB_BA            w,  2,  0, 4                       ;  m2=t1a+t2a (t1),  m0=t1a-t2a (t2)
    SUMSUB_BA            w,  9,  1, 4                       ;  m9=t4a+t5a (t4),  m1=t4a-t5a (t5a)
    SUMSUB_BA            w,  3, 11, 4                       ;  m3=t7a+t6a (t7), m11=t7a-t6a (t6a)
    SUMSUB_BA            w,  1, 11, 4                       ;  m1=t6a+t5a (t6), m11=t6a-t5a (t5)
    pmulhrsw            m1, m12                             ; m1=t6
    pmulhrsw           m11, m12                             ; m11=t5
    VP9_IDCT8_1D_FINALIZE
%endmacro

%macro VP9_IDCT8_4x4_1D 0
    pmulhrsw            m0, m12                             ; m0=t1a/t0a
    pmulhrsw           m10, m2, [pw_15137x2]                ; m10=t3a
    pmulhrsw            m2, [pw_6270x2]                     ; m2=t2a
    pmulhrsw           m11, m1, [pw_16069x2]                ; m11=t7a
    pmulhrsw            m1, [pw_3196x2]                     ; m1=t4a
    pmulhrsw            m9, m3, [pw_9102x2]                 ; m9=-t5a
    pmulhrsw            m3, [pw_13623x2]                    ; m3=t6a
    psubw               m8, m0, m10                         ; m8=t0a-t3a (t3)
    paddw              m10, m0                              ; m10=t0a+t3a (t0)
    SUMSUB_BA            w,  2,  0, 4                       ;  m2=t1a+t2a (t1),  m0=t1a-t2a (t2)
    SUMSUB_BA            w,  9,  1, 4                       ;  m1=t4a+t5a (t4),  m9=t4a-t5a (t5a)
    SWAP                 1,  9
    SUMSUB_BA            w,  3, 11, 4                       ;  m3=t7a+t6a (t7), m11=t7a-t6a (t6a)
    SUMSUB_BA            w,  1, 11, 4                       ;  m1=t6a+t5a (t6), m11=t6a-t5a (t5)
    pmulhrsw            m1, m12                             ; m1=t6
    pmulhrsw           m11, m12                             ; m11=t5
    VP9_IDCT8_1D_FINALIZE
%endmacro

; TODO: a lot of t* copies can probably be removed and merged with
; following SUMSUBs from VP9_IDCT8_1D_FINALIZE with AVX
%macro VP9_IDCT8_2x2_1D 0
    pmulhrsw            m0, m12                             ;  m0=t0
    mova                m3, m1
    pmulhrsw            m1, m6                              ;  m1=t4
    pmulhrsw            m3, m7                              ;  m3=t7
    mova                m2, m0                              ;  m2=t1
    mova               m10, m0                              ; m10=t2
    mova                m8, m0                              ;  m8=t3
    mova               m11, m3                              ; t5 = t7a ...
    mova                m9, m3                              ; t6 = t7a ...
    psubw              m11, m1                              ; t5 = t7a - t4a
    paddw               m9, m1                              ; t6 = t7a + t4a
    pmulhrsw           m11, m12                             ; m11=t5
    pmulhrsw            m9, m12                             ;  m9=t6
    SWAP                 0, 10
    SWAP                 9,  1
    VP9_IDCT8_1D_FINALIZE
%endmacro

%macro VP9_IDCT8_WRITEOUT 0
    mova                m5, [pw_1024]
    pmulhrsw            m0, m5              ; (x*1024 + (1<<14))>>15 <=> (x+16)>>5
    pmulhrsw            m1, m5
    VP9_STORE_2X         0,  1,  6,  7,  4
    lea               dstq, [dstq+2*strideq]
    pmulhrsw            m2, m5
    pmulhrsw            m3, m5
    VP9_STORE_2X         2,  3,  6,  7,  4
    lea               dstq, [dstq+2*strideq]
    pmulhrsw            m8, m5
    pmulhrsw            m9, m5
    VP9_STORE_2X         8,  9,  6,  7,  4
    lea               dstq, [dstq+2*strideq]
    pmulhrsw           m10, m5
    pmulhrsw           m11, m5
    VP9_STORE_2X        10, 11,  6,  7,  4
%endmacro

INIT_XMM ssse3
cglobal vp9_idct_idct_8x8_add, 4,4,13, dst, stride, block, eob

    mova               m12, [pw_11585x2]    ; often used

    cmp eobd, 12 ; top left half or less
    jg .idctfull

    cmp eobd, 3  ; top left corner or less
    jg .idcthalf

    cmp eobd, 1 ; faster path for when only DC is set
    jne .idcttopleftcorner

    movd                m0, [blockq]
    pmulhrsw            m0, m12
    pmulhrsw            m0, m12
    SPLATW              m0, m0, 0
    pxor                m4, m4
    movd          [blockq], m4
    mova                m5, [pw_1024]
    pmulhrsw            m0, m5              ; (x*1024 + (1<<14))>>15 <=> (x+16)>>5
    VP9_STORE_2X         0,  0,  6,  7,  4
    lea               dstq, [dstq+2*strideq]
    VP9_STORE_2X         0,  0,  6,  7,  4
    lea               dstq, [dstq+2*strideq]
    VP9_STORE_2X         0,  0,  6,  7,  4
    lea               dstq, [dstq+2*strideq]
    VP9_STORE_2X         0,  0,  6,  7,  4
    RET

; faster path for when only left corner is set (3 input: DC, right to DC, below
; to DC). Note: also working with a 2x2 block
.idcttopleftcorner:
    movd                m0, [blockq+0]
    movd                m1, [blockq+16]
    mova                m6, [pw_3196x2]
    mova                m7, [pw_16069x2]
    VP9_IDCT8_2x2_1D
    TRANSPOSE8x8W  0, 1, 2, 3, 8, 9, 10, 11, 4
    VP9_IDCT8_2x2_1D
    pxor                m4, m4  ; used for the block reset, and VP9_STORE_2X
    movd       [blockq+ 0], m4
    movd       [blockq+16], m4
    VP9_IDCT8_WRITEOUT
    RET

.idcthalf:
    movh                m0, [blockq + 0]
    movh                m1, [blockq +16]
    movh                m2, [blockq +32]
    movh                m3, [blockq +48]
    VP9_IDCT8_4x4_1D
    TRANSPOSE8x8W  0, 1, 2, 3, 8, 9, 10, 11, 4
    VP9_IDCT8_4x4_1D
    pxor                m4, m4
    movh       [blockq+ 0], m4
    movh       [blockq+16], m4
    movh       [blockq+32], m4
    movh       [blockq+48], m4
    VP9_IDCT8_WRITEOUT
    RET

.idctfull: ; generic full 8x8 idct/idct
    mova                m0, [blockq+  0]    ; IN(0)
    mova                m1, [blockq+ 16]    ; IN(1)
    mova                m2, [blockq+ 32]    ; IN(2)
    mova                m3, [blockq+ 48]    ; IN(3)
    mova                m8, [blockq+ 64]    ; IN(4)
    mova                m9, [blockq+ 80]    ; IN(5)
    mova               m10, [blockq+ 96]    ; IN(6)
    mova               m11, [blockq+112]    ; IN(7)
    mova                m7, [pd_8192]       ; rounding
    VP9_IDCT8_1D
    TRANSPOSE8x8W  0, 1, 2, 3, 8, 9, 10, 11, 4
    VP9_IDCT8_1D
    pxor                m4, m4  ; used for the block reset, and VP9_STORE_2X
    mova      [blockq+  0], m4
    mova      [blockq+ 16], m4
    mova      [blockq+ 32], m4
    mova      [blockq+ 48], m4
    mova      [blockq+ 64], m4
    mova      [blockq+ 80], m4
    mova      [blockq+ 96], m4
    mova      [blockq+112], m4
    VP9_IDCT8_WRITEOUT
    RET

;---------------------------------------------------------------------------------------------
; void vp9_idct_idct_16x16_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;---------------------------------------------------------------------------------------------

%macro VP9_IDCT16_1D 2-3 16 ; src, pass, nnzc
    mova                m5, [%1+ 32]       ; IN(1)
    mova               m14, [%1+ 64]       ; IN(2)
    mova                m6, [%1+ 96]       ; IN(3)
    mova                m9, [%1+128]       ; IN(4)
    mova                m7, [%1+160]       ; IN(5)
    mova               m15, [%1+192]       ; IN(6)
    mova                m4, [%1+224]       ; IN(7)
%if %3 <= 8
    pmulhrsw            m8, m9,  [pw_15137x2]       ; t3
    pmulhrsw            m9, [pw_6270x2]             ; t2
    pmulhrsw           m13, m14, [pw_16069x2]       ; t7
    pmulhrsw           m14, [pw_3196x2]             ; t4
    pmulhrsw           m12, m15, [pw_m9102x2]       ; t5
    pmulhrsw           m15, [pw_13623x2]            ; t6
    pmulhrsw            m2, m5,  [pw_16305x2]       ; t15
    pmulhrsw            m5, [pw_1606x2]             ; t8
    pmulhrsw            m3, m4,  [pw_m10394x2]      ; t9
    pmulhrsw            m4, [pw_12665x2]            ; t14
    pmulhrsw            m0, m7,  [pw_14449x2]       ; t13
    pmulhrsw            m7, [pw_7723x2]             ; t10
    pmulhrsw            m1, m6,  [pw_m4756x2]       ; t11
    pmulhrsw            m6, [pw_15679x2]            ; t12
%else
    mova                m3, [%1+288]       ; IN(9)
    mova               m12, [%1+320]       ; IN(10)
    mova                m0, [%1+352]       ; IN(11)
    mova                m8, [%1+384]       ; IN(12)
    mova                m1, [%1+416]       ; IN(13)
    mova               m13, [%1+448]       ; IN(14)
    mova                m2, [%1+480]       ; IN(15)

    ; m10=in0, m5=in1, m14=in2, m6=in3, m9=in4, m7=in5, m15=in6, m4=in7
    ; m11=in8, m3=in9, m12=in10 m0=in11, m8=in12, m1=in13, m13=in14, m2=in15

    VP9_UNPACK_MULSUB_2W_4X   9,   8, 15137,  6270, [pd_8192], 10, 11 ; t2,  t3
    VP9_UNPACK_MULSUB_2W_4X  14,  13, 16069,  3196, [pd_8192], 10, 11 ; t4,  t7
    VP9_UNPACK_MULSUB_2W_4X  12,  15,  9102, 13623, [pd_8192], 10, 11 ; t5,  t6
    VP9_UNPACK_MULSUB_2W_4X   5,   2, 16305,  1606, [pd_8192], 10, 11 ; t8,  t15
    VP9_UNPACK_MULSUB_2W_4X   3,   4, 10394, 12665, [pd_8192], 10, 11 ; t9,  t14
    VP9_UNPACK_MULSUB_2W_4X   7,   0, 14449,  7723, [pd_8192], 10, 11 ; t10, t13
    VP9_UNPACK_MULSUB_2W_4X   1,   6,  4756, 15679, [pd_8192], 10, 11 ; t11, t12
%endif

    ; m11=t0, m10=t1, m9=t2, m8=t3, m14=t4, m12=t5, m15=t6, m13=t7
    ; m5=t8, m3=t9, m7=t10, m1=t11, m6=t12, m0=t13, m4=t14, m2=t15

    SUMSUB_BA            w, 12, 14, 10      ; t4,  t5
    SUMSUB_BA            w, 15, 13, 10      ; t7,  t6
    SUMSUB_BA            w,  3,  5, 10      ; t8,  t9
    SUMSUB_BA            w,  7,  1, 10      ; t11, t10
    SUMSUB_BA            w,  0,  6, 10      ; t12, t13
    SUMSUB_BA            w,  4,  2, 10      ; t15, t14

    ; m8=t0, m9=t1, m10=t2, m11=t3, m12=t4, m14=t5, m13=t6, m15=t7
    ; m3=t8, m5=t9, m1=t10, m7=t11, m0=t12, m6=t13, m2=t14, m4=t15

    SUMSUB_BA            w, 14, 13, 10
    pmulhrsw           m13, [pw_11585x2]                              ; t5
    pmulhrsw           m14, [pw_11585x2]                              ; t6
    VP9_UNPACK_MULSUB_2W_4X   2,   5, 15137,  6270, [pd_8192], 10, 11 ; t9,  t14
    VP9_UNPACK_MULSUB_2W_4X   6,   1, 6270, m15137, [pd_8192], 10, 11 ; t10, t13

    ; m8=t0, m9=t1, m10=t2, m11=t3, m12=t4, m13=t5, m14=t6, m15=t7
    ; m3=t8, m2=t9, m6=t10, m7=t11, m0=t12, m1=t13, m5=t14, m4=t15

    SUMSUB_BA            w,  7,  3, 10      ; t8,  t11
    SUMSUB_BA            w,  6,  2, 10      ; t9,  t10
    SUMSUB_BA            w,  0,  4, 10      ; t15, t12
    SUMSUB_BA            w,  1,  5, 10      ; t14. t13

    ; m15=t0, m14=t1, m13=t2, m12=t3, m11=t4, m10=t5, m9=t6, m8=t7
    ; m7=t8, m6=t9, m2=t10, m3=t11, m4=t12, m5=t13, m1=t14, m0=t15

    SUMSUB_BA            w,  2,  5, 10
    SUMSUB_BA            w,  3,  4, 10
    pmulhrsw            m5, [pw_11585x2]    ; t10
    pmulhrsw            m4, [pw_11585x2]    ; t11
    pmulhrsw            m3, [pw_11585x2]    ; t12
    pmulhrsw            m2, [pw_11585x2]    ; t13

    ; backup first register
    mova          [rsp+32], m7

    ; m15=t0, m14=t1, m13=t2, m12=t3, m11=t4, m10=t5, m9=t6, m8=t7
    ; m7=t8, m6=t9, m5=t10, m4=t11, m3=t12, m2=t13, m1=t14, m0=t15

    ; from load/start
    mova               m10, [%1+  0]        ; IN(0)
%if %3 <= 8
    pmulhrsw           m10, [pw_11585x2]    ; t0 and t1
    psubw              m11, m10, m8
    paddw               m8, m10
%else
    mova               m11, [%1+256]        ; IN(8)

    ; from 3 stages back
    SUMSUB_BA            w, 11, 10, 7
    pmulhrsw           m11, [pw_11585x2]    ; t0
    pmulhrsw           m10, [pw_11585x2]    ; t1

    ; from 2 stages back
    SUMSUB_BA            w,  8, 11, 7       ; t0,  t3
%endif
    SUMSUB_BA            w,  9, 10, 7       ; t1,  t2

    ; from 1 stage back
    SUMSUB_BA            w, 15,  8, 7       ; t0,  t7
    SUMSUB_BA            w, 14,  9, 7       ; t1,  t6
    SUMSUB_BA            w, 13, 10, 7       ; t2,  t5
    SUMSUB_BA            w, 12, 11, 7       ; t3,  t4

    SUMSUB_BA            w,  0, 15, 7       ; t0, t15
    SUMSUB_BA            w,  1, 14, 7       ; t1, t14
    SUMSUB_BA            w,  2, 13, 7       ; t2, t13
    SUMSUB_BA            w,  3, 12, 7       ; t3, t12
    SUMSUB_BA            w,  4, 11, 7       ; t4, t11
    SUMSUB_BA            w,  5, 10, 7       ; t5, t10

%if %2 == 1
    ; backup a different register
    mova          [rsp+16], m15
    mova                m7, [rsp+32]

    SUMSUB_BA            w,  6,  9, 15      ; t6, t9
    SUMSUB_BA            w,  7,  8, 15      ; t7, t8

    TRANSPOSE8x8W        0, 1, 2, 3, 4, 5, 6, 7, 15
    mova         [rsp+  0], m0
    mova         [rsp+ 32], m1
    mova         [rsp+ 64], m2
    mova         [rsp+ 96], m3
    mova         [rsp+128], m4
    mova         [rsp+160], m5
    mova         [rsp+192], m6
    mova         [rsp+224], m7

    mova               m15, [rsp+16]
    TRANSPOSE8x8W        8, 9, 10, 11, 12, 13, 14, 15, 0
    mova         [rsp+ 16], m8
    mova         [rsp+ 48], m9
    mova         [rsp+ 80], m10
    mova         [rsp+112], m11
    mova         [rsp+144], m12
    mova         [rsp+176], m13
    mova         [rsp+208], m14
    mova         [rsp+240], m15
%else ; %2 == 2
    ; backup more registers
    mova          [rsp+64], m8
    mova          [rsp+96], m9

    pxor                m7, m7
    pmulhrsw            m0, [pw_512]
    pmulhrsw            m1, [pw_512]
    VP9_STORE_2X         0,  1,  8,  9,  7
    lea               dstq, [dstq+strideq*2]
    pmulhrsw            m2, [pw_512]
    pmulhrsw            m3, [pw_512]
    VP9_STORE_2X         2,  3,  8,  9,  7
    lea               dstq, [dstq+strideq*2]
    pmulhrsw            m4, [pw_512]
    pmulhrsw            m5, [pw_512]
    VP9_STORE_2X         4,  5,  8,  9,  7
    lea               dstq, [dstq+strideq*2]

    ; restore from cache
    SWAP                 0, 7               ; move zero from m7 to m0
    mova                m7, [rsp+32]
    mova                m8, [rsp+64]
    mova                m9, [rsp+96]

    SUMSUB_BA            w,  6,  9, 1       ; t6, t9
    SUMSUB_BA            w,  7,  8, 1       ; t7, t8

    pmulhrsw            m6, [pw_512]
    pmulhrsw            m7, [pw_512]
    VP9_STORE_2X         6,  7,  1,  2,  0
    lea               dstq, [dstq+strideq*2]
    pmulhrsw            m8, [pw_512]
    pmulhrsw            m9, [pw_512]
    VP9_STORE_2X         8,  9,  1,  2,  0
    lea               dstq, [dstq+strideq*2]
    pmulhrsw           m10, [pw_512]
    pmulhrsw           m11, [pw_512]
    VP9_STORE_2X        10, 11,  1,  2,  0
    lea               dstq, [dstq+strideq*2]
    pmulhrsw           m12, [pw_512]
    pmulhrsw           m13, [pw_512]
    VP9_STORE_2X        12, 13,  1,  2,  0
    lea               dstq, [dstq+strideq*2]
    pmulhrsw           m14, [pw_512]
    pmulhrsw           m15, [pw_512]
    VP9_STORE_2X        14, 15,  1,  2,  0
%endif ; %2 == 1/2
%endmacro

%macro ZERO_BLOCK 4 ; mem, stride, nnzcpl, zero_reg
%assign %%y 0
%rep %3
%assign %%x 0
%rep %3*2/mmsize
    mova      [%1+%%y+%%x], %4
%assign %%x (%%x+mmsize)
%endrep
%assign %%y (%%y+%2)
%endrep
%endmacro

%macro VP9_STORE_2XFULL 6; dc, tmp1, tmp2, tmp3, tmp4, zero
    mova               m%3, [dstq]
    mova               m%5, [dstq+strideq]
    punpcklbw          m%2, m%3, m%6
    punpckhbw          m%3, m%6
    punpcklbw          m%4, m%5, m%6
    punpckhbw          m%5, m%6
    paddw              m%2, m%1
    paddw              m%3, m%1
    paddw              m%4, m%1
    paddw              m%5, m%1
    packuswb           m%2, m%3
    packuswb           m%4, m%5
    mova            [dstq], m%2
    mova    [dstq+strideq], m%4
%endmacro

INIT_XMM ssse3
cglobal vp9_idct_idct_16x16_add, 4, 5, 16, 512, dst, stride, block, eob
    ; 2x2=eob=3, 4x4=eob=10
    cmp eobd, 38
    jg .idctfull
    cmp eobd, 1 ; faster path for when only DC is set
    jne .idct8x8

    ; dc-only
    movd                m0, [blockq]
    mova                m1, [pw_11585x2]
    pmulhrsw            m0, m1
    pmulhrsw            m0, m1
    SPLATW              m0, m0, q0000
    pmulhrsw            m0, [pw_512]
    pxor                m5, m5
    movd          [blockq], m5
%rep 7
    VP9_STORE_2XFULL    0, 1, 2, 3, 4, 5
    lea               dstq, [dstq+2*strideq]
%endrep
    VP9_STORE_2XFULL    0, 1, 2, 3, 4, 5
    RET

.idct8x8:
    DEFINE_ARGS dst, stride, block, cnt, dst_bak
    VP9_IDCT16_1D   blockq, 1, 8

    mov               cntd, 2
    mov           dst_bakq, dstq
.loop2_8x8:
    VP9_IDCT16_1D      rsp, 2, 8
    lea               dstq, [dst_bakq+8]
    add                rsp, 16
    dec               cntd
    jg .loop2_8x8
    sub                rsp, 32

    ; at the end of the loop, m0 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 32, 8, m0
    RET

.idctfull:
    DEFINE_ARGS dst, stride, block, cnt, dst_bak
    mov               cntd, 2
.loop1_full:
    VP9_IDCT16_1D   blockq, 1
    add             blockq, 16
    add                rsp, 256
    dec               cntd
    jg .loop1_full
    sub             blockq, 32
    sub                rsp, 512

    mov               cntd, 2
    mov           dst_bakq, dstq
.loop2_full:
    VP9_IDCT16_1D      rsp, 2
    lea               dstq, [dst_bakq+8]
    add                rsp, 16
    dec               cntd
    jg .loop2_full
    sub                rsp, 32

    ; at the end of the loop, m0 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 32, 16, m0
    RET

%endif ; x86-64
