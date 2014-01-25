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

pw_11585x2:  times 8 dw 23170
pw_m11585x2: times 8 dw -23170

%macro VP9_IDCT_COEFFS 2-3 0
pw_%1x2:    times 8 dw  %1*2
pw_m%1x2:   times 8 dw -%1*2
pw_%2x2:    times 8 dw  %2*2
pw_m%2x2:   times 8 dw -%2*2
pw_m%1_%2:  times 4 dw -%1,  %2
pw_%2_%1:   times 4 dw  %2,  %1
pw_m%2_m%1: times 4 dw -%2, -%1
%if %3 == 1
pw_m%2_%1:  times 4 dw -%2,  %1
pw_%1_%2:   times 4 dw  %1,  %2
%endif
%endmacro

VP9_IDCT_COEFFS 15137,  6270, 1
VP9_IDCT_COEFFS 16069,  3196, 1
VP9_IDCT_COEFFS  9102, 13623, 1
VP9_IDCT_COEFFS 16305,  1606
VP9_IDCT_COEFFS 10394, 12665
VP9_IDCT_COEFFS 14449,  7723
VP9_IDCT_COEFFS  4756, 15679
VP9_IDCT_COEFFS 16364,   804
VP9_IDCT_COEFFS 11003, 12140
VP9_IDCT_COEFFS 14811,  7005
VP9_IDCT_COEFFS  5520, 15426
VP9_IDCT_COEFFS 15893,  3981
VP9_IDCT_COEFFS  8423, 14053
VP9_IDCT_COEFFS 13160,  9760
VP9_IDCT_COEFFS  2404, 16207

pw_5283_13377: times 4 dw 5283, 13377
pw_9929_13377: times 4 dw 9929, 13377
pw_15212_m13377: times 4 dw 15212, -13377
pw_15212_9929: times 4 dw 15212, 9929
pw_m5283_m15212: times 4 dw -5283, -15212
pw_13377x2: times 8 dw 13377*2

pd_8192: times 4 dd 8192
pw_2048: times 8 dw 2048
pw_1024: times 8 dw 1024
pw_512:  times 8 dw 512
pw_m1:   times 8 dw -1

SECTION .text

; (a*x + b*y + round) >> shift
%macro VP9_MULSUB_2W_2X 5 ; dst1, dst2/src, round, coefs1, coefs2
    pmaddwd            m%1, m%2, %4
    pmaddwd            m%2,  %5
    paddd              m%1,  %3
    paddd              m%2,  %3
    psrad              m%1,  14
    psrad              m%2,  14
%endmacro

%macro VP9_MULSUB_2W_4X 7 ; dst1, dst2, coef1, coef2, rnd, tmp1/src, tmp2
    VP9_MULSUB_2W_2X    %7,  %6,  %5, [pw_m%3_%4], [pw_%4_%3]
    VP9_MULSUB_2W_2X    %1,  %2,  %5, [pw_m%3_%4], [pw_%4_%3]
    packssdw           m%1, m%7
    packssdw           m%2, m%6
%endmacro

%macro VP9_UNPACK_MULSUB_2W_4X 7-9 ; dst1, dst2, (src1, src2,) coef1, coef2, rnd, tmp1, tmp2
%if %0 == 7
    punpckhwd          m%6, m%2, m%1
    punpcklwd          m%2, m%1
    VP9_MULSUB_2W_4X   %1, %2, %3, %4, %5, %6, %7
%else
    punpckhwd          m%8, m%4, m%3
    punpcklwd          m%2, m%4, m%3
    VP9_MULSUB_2W_4X   %1, %2, %5, %6, %7, %8, %9
%endif
%endmacro

%macro VP9_UNPACK_MULSUB_2D_4X 6 ; dst1 [src1], dst2 [src2], dst3, dst4, mul1, mul2
    punpckhwd          m%4, m%2, m%1
    punpcklwd          m%2, m%1
    pmaddwd            m%3, m%4, [pw_m%5_%6]
    pmaddwd            m%4, [pw_%6_%5]
    pmaddwd            m%1, m%2, [pw_m%5_%6]
    pmaddwd            m%2, [pw_%6_%5]
%endmacro

%macro VP9_RND_SH_SUMSUB_BA 6 ; dst1 [src1], dst2 [src2], src3, src4, tmp, round
    SUMSUB_BA            d, %1, %2, %5
    SUMSUB_BA            d, %3, %4, %5
    paddd              m%1, %6
    paddd              m%2, %6
    paddd              m%3, %6
    paddd              m%4, %6
    psrad              m%1, 14
    psrad              m%2, 14
    psrad              m%3, 14
    psrad              m%4, 14
    packssdw           m%1, m%3
    packssdw           m%2, m%4
%endmacro

%macro VP9_STORE_2X 5-6 dstq ; reg1, reg2, tmp1, tmp2, zero, dst
    movh               m%3, [%6]
    movh               m%4, [%6+strideq]
    punpcklbw          m%3, m%5
    punpcklbw          m%4, m%5
    paddw              m%3, m%1
    paddw              m%4, m%2
    packuswb           m%3, m%5
    packuswb           m%4, m%5
    movh              [%6], m%3
    movh      [%6+strideq], m%4
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

;-------------------------------------------------------------------------------------------
; void vp9_iwht_iwht_4x4_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;-------------------------------------------------------------------------------------------

%macro VP9_IWHT4_1D 0
    SWAP                 1, 2, 3
    paddw               m0, m2
    psubw               m3, m1
    psubw               m4, m0, m3
    psraw               m4, 1
    psubw               m5, m4, m1
    SWAP                 5, 1
    psubw               m4, m2
    SWAP                 4, 2
    psubw               m0, m1
    paddw               m3, m2
    SWAP                 3, 2, 1
%endmacro

INIT_MMX mmx
cglobal vp9_iwht_iwht_4x4_add, 3, 3, 0, dst, stride, block, eob
    mova                m0, [blockq+0*8]
    mova                m1, [blockq+1*8]
    mova                m2, [blockq+2*8]
    mova                m3, [blockq+3*8]
    psraw               m0, 2
    psraw               m1, 2
    psraw               m2, 2
    psraw               m3, 2

    VP9_IWHT4_1D
    TRANSPOSE4x4W        0, 1, 2, 3, 4
    VP9_IWHT4_1D

    pxor                m4, m4
    VP9_STORE_2X         0, 1, 5, 6, 4
    lea               dstq, [dstq+strideq*2]
    VP9_STORE_2X         2, 3, 5, 6, 4
    ZERO_BLOCK      blockq, 8, 4, m4
    RET

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

;-------------------------------------------------------------------------------------------
; void vp9_iadst_iadst_4x4_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;-------------------------------------------------------------------------------------------

%macro VP9_IADST4_1D 0
    movq2dq           xmm0, m0
    movq2dq           xmm1, m1
    movq2dq           xmm2, m2
    movq2dq           xmm3, m3
    paddw               m3, m0
    punpcklwd         xmm0, xmm1
    punpcklwd         xmm2, xmm3
    pmaddwd           xmm1, xmm0, [pw_5283_13377]
    pmaddwd           xmm4, xmm0, [pw_9929_13377]
    pmaddwd           xmm0, [pw_15212_m13377]
    pmaddwd           xmm3, xmm2, [pw_15212_9929]
    pmaddwd           xmm2, [pw_m5283_m15212]
    psubw               m3, m2
    paddd             xmm0, xmm2
    paddd             xmm3, [pd_8192]
    paddd             xmm2, [pd_8192]
    paddd             xmm1, xmm3
    paddd             xmm0, xmm3
    paddd             xmm4, xmm2
    psrad             xmm1, 14
    psrad             xmm0, 14
    psrad             xmm4, 14
    pmulhrsw            m3, [pw_13377x2]        ; out2
    packssdw          xmm0, xmm0
    packssdw          xmm1, xmm1
    packssdw          xmm4, xmm4
    movdq2q             m0, xmm0                ; out3
    movdq2q             m1, xmm1                ; out0
    movdq2q             m2, xmm4                ; out1
    SWAP                 0, 1, 2, 3
%endmacro

%macro IADST4_FN 5
INIT_MMX %5
cglobal vp9_%1_%3_4x4_add, 3, 3, 8, dst, stride, block, eob
    mova                m0, [blockq+ 0]
    mova                m1, [blockq+ 8]
    mova                m2, [blockq+16]
    mova                m3, [blockq+24]
    mova                m6, [pw_11585x2]
    mova                m7, [pd_8192]       ; rounding
    VP9_%2_1D
    TRANSPOSE4x4W  0, 1, 2, 3, 4
    VP9_%4_1D
    pxor                m4, m4  ; used for the block reset, and VP9_STORE_2X
    mova       [blockq+ 0], m4
    mova       [blockq+ 8], m4
    mova       [blockq+16], m4
    mova       [blockq+24], m4
    VP9_IDCT4_WRITEOUT
    RET
%endmacro

IADST4_FN idct,  IDCT4,  iadst, IADST4, ssse3
IADST4_FN iadst, IADST4, idct,  IDCT4,  ssse3
IADST4_FN iadst, IADST4, iadst, IADST4, ssse3

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

%macro VP9_IDCT_IDCT_8x8_ADD_XMM 1
INIT_XMM %1
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
    ZERO_BLOCK      blockq, 16, 8, m4
    VP9_IDCT8_WRITEOUT
    RET
%endmacro

VP9_IDCT_IDCT_8x8_ADD_XMM ssse3
VP9_IDCT_IDCT_8x8_ADD_XMM avx

;---------------------------------------------------------------------------------------------
; void vp9_iadst_iadst_8x8_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;---------------------------------------------------------------------------------------------

%macro VP9_IADST8_1D 0 ; input/output=m0/1/2/3/8/9/10/11
    VP9_UNPACK_MULSUB_2D_4X 11,  0,  4,  5, 16305,  1606    ; m11/4=t1[d], m0/5=t0[d]
    VP9_UNPACK_MULSUB_2D_4X  3,  8,  6, 13, 10394, 12665    ; m3/6=t5[d], m8/13=t4[d]
    VP9_RND_SH_SUMSUB_BA     8,  0, 13,  5, 14, m7          ; m8=t0[w], m0=t4[w]
    VP9_RND_SH_SUMSUB_BA     3, 11,  6,  4, 14, m7          ; m3=t1[w], m11=t5[w]

    VP9_UNPACK_MULSUB_2D_4X  9,  2,  4,  5, 14449,  7723    ; m9/4=t3[d], m2/5=t2[d]
    VP9_UNPACK_MULSUB_2D_4X  1, 10,  6, 13,  4756, 15679    ; m1/6=t7[d], m10/13=t6[d]
    VP9_RND_SH_SUMSUB_BA    10,  2, 13,  5, 14, m7          ; m10=t2[w], m2=t6[w]
    VP9_RND_SH_SUMSUB_BA     1,  9,  6,  4, 14, m7          ; m1=t3[w], m9=t7[w]

    ; m8=t0, m3=t1, m10=t2, m1=t3, m0=t4, m11=t5, m2=t6, m9=t7

    VP9_UNPACK_MULSUB_2D_4X  0, 11,  4,  5, 15137,  6270    ; m0/4=t5[d], m11/5=t4[d]
    VP9_UNPACK_MULSUB_2D_4X  9,  2,  6, 13,  6270, 15137    ; m9/6=t6[d], m2/13=t7[d]
    VP9_RND_SH_SUMSUB_BA     9, 11,  6,  5, 14, m7
    psignw                  m9, [pw_m1]                     ; m9=out1[w], m11=t6[w]
    VP9_RND_SH_SUMSUB_BA     2,  0, 13,  4, 14, m7          ; m2=out6[w], m0=t7[w]

    SUMSUB_BA                w, 10,  8, 14                  ; m10=out0[w], m8=t2[w]
    SUMSUB_BA                w,  1,  3, 14
    psignw                  m1, [pw_m1]                     ; m1=out7[w], m3=t3[w]

    ; m10=out0, m9=out1, m8=t2, m3=t3, m11=t6, m0=t7, m2=out6, m1=out7

    SUMSUB_BA                w,  3,  8,  4
    SUMSUB_BA                w,  0, 11,  5
    pmulhrsw                m3, m12
    pmulhrsw               m11, m12
    pmulhrsw                m8, m12                         ; out4
    pmulhrsw                m0, m12                         ; out2
    psignw                  m3, [pw_m1]                     ; out3
    psignw                 m11, [pw_m1]                     ; out5

    ; m10=out0, m9=out1, m0=out2, m3=out3, m8=out4, m11=out5, m2=out6, m1=out7

    SWAP                     0, 10, 2
    SWAP                    11,  1, 9
%endmacro

%macro IADST8_FN 5
INIT_XMM %5
cglobal vp9_%1_%3_8x8_add, 3, 3, 15, dst, stride, block, eob
    mova                m0, [blockq+  0]    ; IN(0)
    mova                m1, [blockq+ 16]    ; IN(1)
    mova                m2, [blockq+ 32]    ; IN(2)
    mova                m3, [blockq+ 48]    ; IN(3)
    mova                m8, [blockq+ 64]    ; IN(4)
    mova                m9, [blockq+ 80]    ; IN(5)
    mova               m10, [blockq+ 96]    ; IN(6)
    mova               m11, [blockq+112]    ; IN(7)

    mova               m12, [pw_11585x2]    ; often used
    mova                m7, [pd_8192]       ; rounding
    VP9_%2_1D
    TRANSPOSE8x8W  0, 1, 2, 3, 8, 9, 10, 11, 4
    VP9_%4_1D

    pxor                m4, m4  ; used for the block reset, and VP9_STORE_2X
    ZERO_BLOCK      blockq, 16, 8, m4
    VP9_IDCT8_WRITEOUT
    RET
%endmacro

IADST8_FN idct,  IDCT8,  iadst, IADST8, ssse3
IADST8_FN idct,  IDCT8,  iadst, IADST8, avx
IADST8_FN iadst, IADST8, idct,  IDCT8,  ssse3
IADST8_FN iadst, IADST8, idct,  IDCT8,  avx
IADST8_FN iadst, IADST8, iadst, IADST8, ssse3
IADST8_FN iadst, IADST8, iadst, IADST8, avx

;---------------------------------------------------------------------------------------------
; void vp9_idct_idct_16x16_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;---------------------------------------------------------------------------------------------

; at the end of this macro, m7 is stored in stack_scratch
; everything else (t0-6 and t8-15) is stored in m0-6 and m8-15
; the following sumsubs have not been done yet:
;    SUMSUB_BA            w,  6,  9, 15      ; t6, t9
;    SUMSUB_BA            w,  7,  8, 15      ; t7, t8
%macro VP9_IDCT16_1D_START 4 ; src, nnzc, stride, stack_scratch
%if %2 <= 4
    mova                m3, [%1+ 1*%3]      ; IN(1)
    mova               m12, [%1+ 2*%3]      ; IN(2)
    mova                m0, [%1+ 3*%3]      ; IN(3)

    pmulhrsw           m15, m12, [pw_16069x2]       ; t6-7
    pmulhrsw           m12, [pw_3196x2]             ; t4-5
    pmulhrsw            m4, m3,  [pw_16305x2]       ; t14-15
    pmulhrsw            m3, [pw_1606x2]             ; t8-9
    pmulhrsw            m7, m0,  [pw_m4756x2]       ; t10-11
    pmulhrsw            m0, [pw_15679x2]            ; t12-13

    ; m8=t0, m9=t1, m10=t2, m11=t3, m12=t4, m14=t5, m13=t6, m15=t7
    ; m3=t8, m5=t9, m1=t10, m7=t11, m0=t12, m6=t13, m2=t14, m4=t15

    paddw              m14, m15, m12
    psubw              m13, m15, m12
    pmulhrsw           m13, [pw_11585x2]            ; t5
    pmulhrsw           m14, [pw_11585x2]            ; t6

    VP9_UNPACK_MULSUB_2W_4X 2, 5, 4, 3, 15137,  6270, [pd_8192], 10, 11 ; t9,  t14
    VP9_UNPACK_MULSUB_2W_4X 6, 1, 0, 7, 6270, m15137, [pd_8192], 10, 11 ; t10, t13

    ; m15=t0, m14=t1, m13=t2, m12=t3, m11=t4, m10=t5, m9=t6, m8=t7
    ; m7=t8, m6=t9, m2=t10, m3=t11, m4=t12, m5=t13, m1=t14, m0=t15
%else
    mova                m5, [%1+ 1*%3]     ; IN(1)
    mova               m14, [%1+ 2*%3]     ; IN(2)
    mova                m6, [%1+ 3*%3]     ; IN(3)
    mova                m9, [%1+ 4*%3]     ; IN(4)
    mova                m7, [%1+ 5*%3]     ; IN(5)
    mova               m15, [%1+ 6*%3]     ; IN(6)
    mova                m4, [%1+ 7*%3]     ; IN(7)
%if %2 <= 8
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
    mova                m3, [%1+ 9*%3]     ; IN(9)
    mova               m12, [%1+10*%3]     ; IN(10)
    mova                m0, [%1+11*%3]     ; IN(11)
    mova                m8, [%1+12*%3]     ; IN(12)
    mova                m1, [%1+13*%3]     ; IN(13)
    mova               m13, [%1+14*%3]     ; IN(14)
    mova                m2, [%1+15*%3]     ; IN(15)

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
%endif

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
    mova              [%4], m7

    ; m15=t0, m14=t1, m13=t2, m12=t3, m11=t4, m10=t5, m9=t6, m8=t7
    ; m7=t8, m6=t9, m5=t10, m4=t11, m3=t12, m2=t13, m1=t14, m0=t15

    ; from load/start
%if %2 <= 4
    mova               m11, [%1+ 0*%3]      ; IN(0)
    pmulhrsw           m11, [pw_11585x2]    ; t0-t3

    psubw               m8, m11, m15
    paddw              m15, m11
    psubw               m9, m11, m14
    paddw              m14, m11
    psubw              m10, m11, m13
    paddw              m13, m11
%else
    mova               m10, [%1+ 0*%3]      ; IN(0)
%if %2 <= 8
    pmulhrsw           m10, [pw_11585x2]    ; t0 and t1
    psubw              m11, m10, m8
    paddw               m8, m10
%else
    mova               m11, [%1+ 8*%3]      ; IN(8)

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
%endif
    SUMSUB_BA            w, 12, 11, 7       ; t3,  t4

    SUMSUB_BA            w,  0, 15, 7       ; t0, t15
    SUMSUB_BA            w,  1, 14, 7       ; t1, t14
    SUMSUB_BA            w,  2, 13, 7       ; t2, t13
    SUMSUB_BA            w,  3, 12, 7       ; t3, t12
    SUMSUB_BA            w,  4, 11, 7       ; t4, t11
    SUMSUB_BA            w,  5, 10, 7       ; t5, t10
%endmacro

%macro VP9_IDCT16_1D 2-3 16 ; src, pass, nnzc
    VP9_IDCT16_1D_START %1, %3, 32, tmpq+32

%if %2 == 1
    ; backup a different register
    mova         [tmpq+16], m15
    mova                m7, [tmpq+32]

    SUMSUB_BA            w,  6,  9, 15      ; t6, t9
    SUMSUB_BA            w,  7,  8, 15      ; t7, t8

    TRANSPOSE8x8W        0, 1, 2, 3, 4, 5, 6, 7, 15
    mova        [tmpq+  0], m0
    mova        [tmpq+ 32], m1
    mova        [tmpq+ 64], m2
    mova        [tmpq+ 96], m3
    mova        [tmpq+128], m4
    mova        [tmpq+160], m5
    mova        [tmpq+192], m6
    mova        [tmpq+224], m7

    mova               m15, [tmpq+16]
    TRANSPOSE8x8W        8, 9, 10, 11, 12, 13, 14, 15, 0
    mova        [tmpq+ 16], m8
    mova        [tmpq+ 48], m9
    mova        [tmpq+ 80], m10
    mova        [tmpq+112], m11
    mova        [tmpq+144], m12
    mova        [tmpq+176], m13
    mova        [tmpq+208], m14
    mova        [tmpq+240], m15
%else ; %2 == 2
    ; backup more registers
    mova         [tmpq+64], m8
    mova         [tmpq+96], m9

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
    mova                m7, [tmpq+32]
    mova                m8, [tmpq+64]
    mova                m9, [tmpq+96]

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

%macro VP9_STORE_2XFULL 6-7 strideq; dc, tmp1, tmp2, tmp3, tmp4, zero, stride
    mova               m%3, [dstq]
    mova               m%5, [dstq+%7]
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
    mova         [dstq+%7], m%4
%endmacro

%macro VP9_IDCT_IDCT_16x16_ADD_XMM 1
INIT_XMM %1
cglobal vp9_idct_idct_16x16_add, 4, 6, 16, 512, dst, stride, block, eob
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

    DEFINE_ARGS dst, stride, block, cnt, dst_bak, tmp
.idct8x8:
    mov               tmpq, rsp
    VP9_IDCT16_1D   blockq, 1, 8

    mov               cntd, 2
    mov           dst_bakq, dstq
.loop2_8x8:
    VP9_IDCT16_1D     tmpq, 2, 8
    lea               dstq, [dst_bakq+8]
    add               tmpq, 16
    dec               cntd
    jg .loop2_8x8

    ; at the end of the loop, m0 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 32, 8, m0
    RET

.idctfull:
    mov               cntd, 2
    mov               tmpq, rsp
.loop1_full:
    VP9_IDCT16_1D   blockq, 1
    add             blockq, 16
    add               tmpq, 256
    dec               cntd
    jg .loop1_full
    sub             blockq, 32

    mov               cntd, 2
    mov               tmpq, rsp
    mov           dst_bakq, dstq
.loop2_full:
    VP9_IDCT16_1D     tmpq, 2
    lea               dstq, [dst_bakq+8]
    add               tmpq, 16
    dec               cntd
    jg .loop2_full

    ; at the end of the loop, m0 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 32, 16, m0
    RET
%endmacro

VP9_IDCT_IDCT_16x16_ADD_XMM ssse3
VP9_IDCT_IDCT_16x16_ADD_XMM avx

;---------------------------------------------------------------------------------------------
; void vp9_iadst_iadst_16x16_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;---------------------------------------------------------------------------------------------

%macro VP9_IADST16_1D 2 ; src, pass
%assign %%str 16*%2
    mova                m0, [%1+ 0*32]  ; in0
    mova                m1, [%1+15*32]  ; in15
    mova                m8, [%1+ 7*32]  ; in7
    mova                m9, [%1+ 8*32]  ; in8

    VP9_UNPACK_MULSUB_2D_4X  1,  0,  2,  3, 16364,   804    ; m1/2=t1[d], m0/3=t0[d]
    VP9_UNPACK_MULSUB_2D_4X  8,  9, 11, 10, 11003, 12140    ; m8/11=t9[d], m9/10=t8[d]
    VP9_RND_SH_SUMSUB_BA     9,  0, 10,  3,  4, [pd_8192]   ; m9=t0[w], m0=t8[w]
    VP9_RND_SH_SUMSUB_BA     8,  1, 11,  2,  4, [pd_8192]   ; m8=t1[w], m1=t9[w]

    mova               m11, [%1+ 2*32]  ; in2
    mova               m10, [%1+13*32]  ; in13
    mova                m3, [%1+ 5*32]  ; in5
    mova                m2, [%1+10*32]  ; in10

    VP9_UNPACK_MULSUB_2D_4X 10, 11,  6,  7, 15893,  3981    ; m10/6=t3[d], m11/7=t2[d]
    VP9_UNPACK_MULSUB_2D_4X  3,  2,  4,  5,  8423, 14053    ; m3/4=t11[d], m2/5=t10[d]
    VP9_RND_SH_SUMSUB_BA     2, 11,  5,  7, 12, [pd_8192]   ; m2=t2[w], m11=t10[w]
    VP9_RND_SH_SUMSUB_BA     3, 10,  4,  6, 12, [pd_8192]   ; m3=t3[w], m10=t11[w]

    mova   [tmpq+ 0*%%str], m9          ; make some scratch space (t0:m9->r0)
    mova                m4, [%1+ 4*32]  ; in4
    mova                m5, [%1+11*32]  ; in11
    mova               m12, [%1+ 3*32]  ; in3
    mova               m13, [%1+12*32]  ; in12

    VP9_UNPACK_MULSUB_2D_4X  5,  4,  7,  6, 14811,  7005    ; m5/7=t5[d], m4/6=t4[d]
    VP9_UNPACK_MULSUB_2D_4X 12, 13, 14, 15,  5520, 15426    ; m12/14=t13[d], m13/15=t12[d]
    VP9_RND_SH_SUMSUB_BA    13,  4, 15,  6,  9, [pd_8192]   ; m13=t4[w], m4=t12[w]
    VP9_RND_SH_SUMSUB_BA    12,  5, 14,  7,  9, [pd_8192]   ; m12=t5[w], m5=t13[w]

    mova   [tmpq+ 2*%%str], m8          ; t1:m9->r2
    mova   [tmpq+ 3*%%str], m2          ; t2:m2->r3
    mova   [tmpq+ 4*%%str], m3          ; t3:m3->r4
    mova   [tmpq+ 5*%%str], m13         ; t4:m13->r5
    mova                m2, [%1+ 6*32]  ; in6
    mova                m3, [%1+ 9*32]  ; in9
    mova                m8, [%1+ 1*32]  ; in1
    mova                m9, [%1+14*32]  ; in14

    VP9_UNPACK_MULSUB_2D_4X  3,  2,  7,  6, 13160,  9760    ; m3/7=t7[d], m2/6=t6[d]
    VP9_UNPACK_MULSUB_2D_4X  8,  9, 13, 14,  2404, 16207    ; m8/13=t15[d], m9/14=t14[d]
    VP9_RND_SH_SUMSUB_BA     9,  2, 14,  6, 15, [pd_8192]   ; m9=t6[w], m2=t14[w]
    VP9_RND_SH_SUMSUB_BA     8,  3, 13,  7, 15, [pd_8192]   ; m8=t7[w], m3=t15[w]

    ; r0=t0, r2=t1, r3=t2, r4=t3, r5=t4, m12=t5, m9=t6, m8=t7
    ; m0=t8, m1=t9, m11=t10, m10=t11, m4=t12, m5=t13, m2=t14, m3=t15

    ; handle t8-15 first
    VP9_UNPACK_MULSUB_2D_4X  0,  1,  6,  7, 16069,  3196    ; m1/7=t8[d], m0/6=t9[d]
    VP9_UNPACK_MULSUB_2D_4X  5,  4, 13, 14,  3196, 16069    ; m5/13=t12[d], m4/14=t13[d]
    VP9_RND_SH_SUMSUB_BA     5,  1, 13,  7, 15, [pd_8192]   ; m5=t8[w], m1=t12[w]
    VP9_RND_SH_SUMSUB_BA     4,  0, 14,  6, 15, [pd_8192]   ; m4=t9[w], m0=t13[w]

    VP9_UNPACK_MULSUB_2D_4X 11, 10,  6,  7,  9102, 13623    ; m11/6=t11[d], m10/7=t10[d]
    VP9_UNPACK_MULSUB_2D_4X  3,  2, 13, 14, 13623,  9102    ; m3/13=t14[d], m2/14=t15[d]
    VP9_RND_SH_SUMSUB_BA     3, 10, 13,  7, 15, [pd_8192]   ; m3=t10[w], m10=t14[w]
    VP9_RND_SH_SUMSUB_BA     2, 11, 14,  6, 15, [pd_8192]   ; m2=t11[w], m11=t15[w]

    ; m5=t8, m4=t9, m3=t10, m2=t11, m1=t12, m0=t13, m10=t14, m11=t15

    VP9_UNPACK_MULSUB_2D_4X  1,  0,  6,  7, 15137,  6270    ; m1/6=t13[d], m0/7=t12[d]
    VP9_UNPACK_MULSUB_2D_4X 11, 10, 13, 14,  6270, 15137    ; m11/13=t14[d], m10/14=t15[d]
    VP9_RND_SH_SUMSUB_BA    11,  0, 13,  7, 15, [pd_8192]   ; m11=out2[w], m0=t14[w]
    VP9_RND_SH_SUMSUB_BA    10,  1, 14,  6, 15, [pd_8192]
    psignw                 m10, [pw_m1]                     ; m10=out13[w], m1=t15[w]

    SUMSUB_BA                w,  3,  5, 15
    psignw                  m3, [pw_m1]                     ; m3=out1[w], m5=t10[w]
    SUMSUB_BA                w,  2,  4, 15                  ; m2=out14[w], m4=t11[w]

    SUMSUB_BA                w,  5,  4, 15
    pmulhrsw                m5, [pw_11585x2]                ; m5=out6[w]
    pmulhrsw                m4, [pw_11585x2]                ; m4=out9[w]
    SUMSUB_BA                w,  1,  0, 15
    pmulhrsw                m1, [pw_m11585x2]               ; m1=out5[w]
    pmulhrsw                m0, [pw_11585x2]                ; m0=out10[w]

    ; m3=out1, m11=out2, m1=out5, m5=out6, m4=out9, m0=out10, m10=out13, m2=out14

    mova                    m6, [tmpq+ 0*%%str]
    mova                    m7, [tmpq+ 2*%%str]
    mova                   m13, [tmpq+ 3*%%str]
    mova                   m14, [tmpq+ 4*%%str]
    mova                   m15, [tmpq+ 5*%%str]
    mova       [tmpq+ 8*%%str], m5
    mova       [tmpq+ 9*%%str], m4
    mova       [tmpq+10*%%str], m0
    mova       [tmpq+11*%%str], m10
    mova       [tmpq+12*%%str], m2

    ; m6=t0, m7=t1, m13=t2, m14=t3, m15=t4, m12=t5, m9=t6, m8=t7
    ; m3=out1, m11=out2, m1=out5, r8=out6, r9=out9, r10=out10, r11=out13, r12=out14

    SUMSUB_BA                w, 15,  6,  0                  ; m15=t0[w], m6=t4[w]
    SUMSUB_BA                w, 12,  7,  0                  ; m12=t1[w], m7=t5[w]
    SUMSUB_BA                w,  9, 13,  0                  ; m9=t2[w], m13=t6[w]
    SUMSUB_BA                w,  8, 14,  0                  ; m8=t3[w], m14=t7[w]

    VP9_UNPACK_MULSUB_2D_4X  6,  7,  0,  2, 15137,  6270    ; m6/0=t5[d], m7/2=t4[d]
    VP9_UNPACK_MULSUB_2D_4X 14, 13,  4,  5,  6270, 15137    ; m14/4=t6[d], m13/5=t7[d]
    VP9_RND_SH_SUMSUB_BA    14,  7,  4,  2, 10, [pd_8192]
    psignw                 m14, [pw_m1]                     ; m14=out3[w], m7=t6[w]
    VP9_RND_SH_SUMSUB_BA    13,  6,  5,  0, 10, [pd_8192]   ; m13=out12[w], m6=t7[w]
    SUMSUB_BA                w,  9, 15, 10                  ; m9=out0[w], m15=t2[w]
    SUMSUB_BA                w,  8, 12, 10
    psignw                  m8, [pw_m1]                     ; m8=out15[w], m12=t3[w]

    SUMSUB_BA                w, 12, 15, 10
    pmulhrsw               m12, [pw_m11585x2]               ; m12=out7[w]
    pmulhrsw               m15, [pw_11585x2]                ; m15=out8[w]
    SUMSUB_BA                w,  7,  6, 10
    pmulhrsw                m7, [pw_11585x2]                ; m7=out4[w]
    pmulhrsw                m6, [pw_11585x2]                ; m6=out11[w]

    ; m9=out0, m14=out3, m7=out4, m12=out7, m15=out8, m6=out11, m13=out12, m8=out15
    ; m3=out1, m11=out2, m1=out5, r8=out6, r9=out9, r10=out10, r11=out13, r12=out14

%if %2 == 1
    mova                    m0, [tmpq+ 8*%%str]
    TRANSPOSE8x8W            9, 3, 11, 14, 7, 1, 0, 12, 2
    mova          [tmpq+ 0*16], m9
    mova          [tmpq+ 2*16], m3
    mova          [tmpq+ 4*16], m11
    mova          [tmpq+ 6*16], m14
    mova                    m9, [tmpq+ 9*%%str]
    mova                    m3, [tmpq+10*%%str]
    mova                   m11, [tmpq+11*%%str]
    mova                   m14, [tmpq+12*%%str]
    mova          [tmpq+ 8*16], m7
    mova          [tmpq+10*16], m1
    mova          [tmpq+12*16], m0
    mova          [tmpq+14*16], m12

    TRANSPOSE8x8W           15, 9, 3, 6, 13, 11, 14, 8, 2
    mova          [tmpq+ 1*16], m15
    mova          [tmpq+ 3*16], m9
    mova          [tmpq+ 5*16], m3
    mova          [tmpq+ 7*16], m6
    mova          [tmpq+ 9*16], m13
    mova          [tmpq+11*16], m11
    mova          [tmpq+13*16], m14
    mova          [tmpq+15*16], m8
%else
    mova                    m5, [tmpq+ 8*%%str]
    pxor                    m0, m0

    pmulhrsw                m9, [pw_512]
    pmulhrsw                m3, [pw_512]
    VP9_STORE_2X             9,  3, 2, 4, 0
    lea                   dstq, [dstq+strideq*2]
    pmulhrsw               m11, [pw_512]
    pmulhrsw               m14, [pw_512]
    VP9_STORE_2X            11, 14, 2, 4, 0
    lea                   dstq, [dstq+strideq*2]
    pmulhrsw                m7, [pw_512]
    pmulhrsw                m1, [pw_512]
    VP9_STORE_2X             7,  1, 2, 4, 0
    lea                   dstq, [dstq+strideq*2]
    pmulhrsw                m5, [pw_512]
    pmulhrsw               m12, [pw_512]
    VP9_STORE_2X             5, 12, 2, 4, 0
    lea                   dstq, [dstq+strideq*2]

    mova                    m9, [tmpq+ 9*%%str]
    mova                    m3, [tmpq+10*%%str]
    mova                   m11, [tmpq+11*%%str]
    mova                   m14, [tmpq+12*%%str]

    pmulhrsw               m15, [pw_512]
    pmulhrsw                m9, [pw_512]
    VP9_STORE_2X            15,  9, 2, 4, 0
    lea                   dstq, [dstq+strideq*2]
    pmulhrsw                m3, [pw_512]
    pmulhrsw                m6, [pw_512]
    VP9_STORE_2X             3,  6, 2, 4, 0
    lea                   dstq, [dstq+strideq*2]
    pmulhrsw               m13, [pw_512]
    pmulhrsw               m11, [pw_512]
    VP9_STORE_2X            13, 11, 2, 4, 0
    lea                   dstq, [dstq+strideq*2]
    pmulhrsw               m14, [pw_512]
    pmulhrsw                m8, [pw_512]
    VP9_STORE_2X            14,  8, 2, 4, 0
%endif
%endmacro

%macro IADST16_FN 5
INIT_XMM %5
cglobal vp9_%1_%3_16x16_add, 3, 6, 16, 512, dst, stride, block, cnt, dst_bak, tmp
    mov               cntd, 2
    mov               tmpq, rsp
.loop1_full:
    VP9_%2_1D       blockq, 1
    add             blockq, 16
    add               tmpq, 256
    dec               cntd
    jg .loop1_full
    sub             blockq, 32

    mov               cntd, 2
    mov               tmpq, rsp
    mov           dst_bakq, dstq
.loop2_full:
    VP9_%4_1D         tmpq, 2
    lea               dstq, [dst_bakq+8]
    add               tmpq, 16
    dec               cntd
    jg .loop2_full

    ; at the end of the loop, m0 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 32, 16, m0
    RET
%endmacro

IADST16_FN idct,  IDCT16,  iadst, IADST16, ssse3
IADST16_FN idct,  IDCT16,  iadst, IADST16, avx
IADST16_FN iadst, IADST16, idct,  IDCT16,  ssse3
IADST16_FN iadst, IADST16, idct,  IDCT16,  avx
IADST16_FN iadst, IADST16, iadst, IADST16, ssse3
IADST16_FN iadst, IADST16, iadst, IADST16, avx

;---------------------------------------------------------------------------------------------
; void vp9_idct_idct_32x32_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;---------------------------------------------------------------------------------------------

%macro VP9_IDCT32_1D 2-3 32 ; src, pass, nnzc
%assign %%str 16*%2*%2
    ; first do t0-15, this can be done identical to idct16x16
    VP9_IDCT16_1D_START %1, %3/2, 64*2, tmpq+ 4*%%str

    ; backup a different register
    mova    [tmpq+30*%%str], m15    ; t15
    mova                m7, [tmpq+ 4*%%str]

    SUMSUB_BA            w,  6,  9, 15      ; t6, t9
    SUMSUB_BA            w,  7,  8, 15      ; t7, t8

    ; store everything on stack to make space available for t16-31
    ; we store interleaved with the output of the second half (t16-31)
    ; so we don't need to allocate extra stack space
    mova    [tmpq+ 0*%%str], m0     ; t0
    mova    [tmpq+ 4*%%str], m1     ; t1
    mova    [tmpq+ 8*%%str], m2     ; t2
    mova    [tmpq+12*%%str], m3     ; t3
    mova    [tmpq+16*%%str], m4     ; t4
    mova    [tmpq+20*%%str], m5     ; t5
    mova    [tmpq+24*%%str], m6     ; t6
    mova    [tmpq+28*%%str], m7     ; t7
    mova    [tmpq+ 2*%%str], m8     ; t8
    mova    [tmpq+ 6*%%str], m9     ; t9
    mova    [tmpq+10*%%str], m10    ; t10
    mova    [tmpq+14*%%str], m11    ; t11
    mova    [tmpq+18*%%str], m12    ; t12
    mova    [tmpq+22*%%str], m13    ; t13
    mova    [tmpq+26*%%str], m14    ; t14

    ; then, secondly, do t16-31
%if %3 <= 8
    mova                 m4, [%1+ 1*64]
    mova                 m3, [%1+ 3*64]
    mova                 m0, [%1+ 5*64]
    mova                 m7, [%1+ 7*64]

    pmulhrsw            m11,  m4, [pw_16364x2] ;t31
    pmulhrsw             m4, [pw_804x2] ;t16
    pmulhrsw             m8,  m7, [pw_m5520x2] ;t19
    pmulhrsw             m7, [pw_15426x2] ;t28
    pmulhrsw            m15,  m0, [pw_15893x2] ;t27
    pmulhrsw             m0, [pw_3981x2] ;t20
    pmulhrsw            m12,  m3, [pw_m2404x2] ;t23
    pmulhrsw             m3, [pw_16207x2] ;t24

    ; m4=t16/17, m8=t18/19, m0=t20/21, m12=t22/23,
    ; m3=t24/25, m15=t26/27, m7=t28/29, m11=t30/31

    VP9_UNPACK_MULSUB_2W_4X   5, 10, 11,  4, 16069,  3196, [pd_8192], 6,  9 ; t17, t30
    VP9_UNPACK_MULSUB_2W_4X   9,  6,  7,  8, 3196, m16069, [pd_8192], 1, 14 ; t18, t29
    ; from 1 stage forward
    SUMSUB_BA                 w,  8,  4,  1
    ; temporary storage
    mova    [tmpq+17*%%str], m8             ; t16
    mova    [tmpq+21*%%str], m4             ; t19
    VP9_UNPACK_MULSUB_2W_4X   1, 14, 15,  0,  9102, 13623, [pd_8192], 4,  8 ; t21, t26
    VP9_UNPACK_MULSUB_2W_4X  13,  2,  3, 12, 13623, m9102, [pd_8192], 4,  8 ; t22, t25

    ; m4=t16, m5=t17, m9=t18, m8=t19, m0=t20, m1=t21, m13=t22, m12=t23,
    ; m3=t24, m2=t25, m14=t26, m15=t27, m7=t28, m6=t29, m10=t30, m11=t31
%else
    mova                m10, [%1+ 1*64]
    mova                m13, [%1+ 3*64]
    mova                m14, [%1+ 5*64]
    mova                 m9, [%1+ 7*64]
    mova                 m8, [%1+ 9*64]
    mova                m15, [%1+11*64]
    mova                m12, [%1+13*64]
    mova                m11, [%1+15*64]
%if %3 <= 16
    pmulhrsw             m5, m10, [pw_16364x2]
    pmulhrsw            m10, [pw_804x2]
    pmulhrsw             m4, m11, [pw_m11003x2]
    pmulhrsw            m11, [pw_12140x2]
    pmulhrsw             m7,  m8, [pw_14811x2]
    pmulhrsw             m8, [pw_7005x2]
    pmulhrsw             m6,  m9, [pw_m5520x2]
    pmulhrsw             m9, [pw_15426x2]
    pmulhrsw             m1, m14, [pw_15893x2]
    pmulhrsw            m14, [pw_3981x2]
    pmulhrsw             m0, m15, [pw_m8423x2]
    pmulhrsw            m15, [pw_14053x2]
%else
    mova                 m4, [%1+17*64]
    mova                 m0, [%1+21*64]
    mova                 m7, [%1+23*64]
    mova                 m6, [%1+25*64]
    mova                 m1, [%1+27*64]
    mova                 m5, [%1+31*64]

    ; m10=in1, m4=in17, m8=in9, m6=in25, m14=in5, m0=in21, m12=in13, m2=in29,
    ; m13=in3, m3=in19, m15=in11, m1=in27, m9=in7, m7=in23, m11=in15, m5=in31

    VP9_UNPACK_MULSUB_2W_4X  10,  5, 16364,   804, [pd_8192], 2, 3 ; t16, t31
    VP9_UNPACK_MULSUB_2W_4X   4, 11, 11003, 12140, [pd_8192], 2, 3 ; t17, t30
    VP9_UNPACK_MULSUB_2W_4X   8,  7, 14811,  7005, [pd_8192], 2, 3 ; t18, t29
    VP9_UNPACK_MULSUB_2W_4X   6,  9,  5520, 15426, [pd_8192], 2, 3 ; t19, t28
    VP9_UNPACK_MULSUB_2W_4X  14,  1, 15893,  3981, [pd_8192], 2, 3 ; t20, t27
    VP9_UNPACK_MULSUB_2W_4X   0, 15,  8423, 14053, [pd_8192], 2, 3 ; t21, t26
%endif

    ; from 1 stage forward
    SUMSUB_BA             w,  4, 10,  2
    SUMSUB_BA             w,  8,  6,  2
    ; from 2 stages forward
    SUMSUB_BA             w,  8,  4,  2
    ; temporary storage
    mova    [tmpq+17*%%str], m8             ; t16
    mova    [tmpq+21*%%str], m4             ; t19
%if %3 <= 16
    pmulhrsw             m3, m12, [pw_13160x2]
    pmulhrsw            m12, [pw_9760x2]
    pmulhrsw             m2, m13, [pw_m2404x2]
    pmulhrsw            m13, [pw_16207x2]
%else
    mova                 m2, [%1+29*64]
    mova                 m3, [%1+19*64]
    VP9_UNPACK_MULSUB_2W_4X  12,  3, 13160,  9760, [pd_8192], 4, 8 ; t22, t25
    VP9_UNPACK_MULSUB_2W_4X   2, 13,  2404, 16207, [pd_8192], 4, 8 ; t23, t24
%endif

    ; m10=t16, m4=t17, m8=t18, m6=t19, m14=t20, m0=t21, m12=t22, m2=t23,
    ; m13=t24, m3=t25, m15=t26, m1=t27, m9=t28, m7=t29, m11=t30, m5=t31

    SUMSUB_BA             w,  0, 14,  4
    SUMSUB_BA             w, 12,  2,  4
    SUMSUB_BA             w,  3, 13,  4
    SUMSUB_BA             w, 15,  1,  4
    SUMSUB_BA             w,  7,  9,  4
    SUMSUB_BA             w, 11,  5,  4

    ; m4=t16, m10=t17, m6=t18, m8=t19, m0=t20, m14=t21, m2=t22, m12=t23,
    ; m3=t24, m13=t25, m1=t26, m15=t27, m7=t28, m9=t29, m5=t30, m11=t31

    VP9_UNPACK_MULSUB_2W_4X   5, 10, 16069,  3196, [pd_8192], 4, 8 ; t17, t30
    VP9_UNPACK_MULSUB_2W_4X   9,  6, 3196, m16069, [pd_8192], 4, 8 ; t18, t29
    VP9_UNPACK_MULSUB_2W_4X   1, 14,  9102, 13623, [pd_8192], 4, 8 ; t21, t26
    VP9_UNPACK_MULSUB_2W_4X  13,  2, 13623, m9102, [pd_8192], 4, 8 ; t22, t25
%endif

    ; m4=t16, m5=t17, m9=t18, m8=t19, m0=t20, m1=t21, m13=t22, m12=t23,
    ; m3=t24, m2=t25, m14=t26, m15=t27, m7=t28, m6=t29, m10=t30, m11=t31

    SUMSUB_BA             w,  9,  5,  4
    SUMSUB_BA             w,  1, 13,  4
    SUMSUB_BA             w,  0, 12,  4
    SUMSUB_BA             w, 15,  3,  4
    SUMSUB_BA             w, 14,  2,  4
    SUMSUB_BA             w,  6, 10,  4
    SUMSUB_BA             w,  7, 11,  4

    ; m8[s]=t16, m9=t17, m5=t18, m4[s]=t19, m12=t20, m13=t21, m1=t22, m0=t23,
    ; m15=t24, m14=t25, m2=t26, m3=t27, m11=t28, m10=t29, m6=t30, m7=t31

    mova                 m8, [tmpq+17*%%str] ; t16
    ; from 2 stages forward
    SUMSUB_BA             w,  0,  8,  4
    SUMSUB_BA             w, 15,  7,  4
    ; from 3 stages forward
    SUMSUB_BA             w,  8,  7,  4
    pmulhrsw             m7, [pw_11585x2]
    pmulhrsw             m8, [pw_11585x2]
    ; store t16/t23
    mova    [tmpq+ 1*%%str], m0     ; t16
    mova    [tmpq+29*%%str], m7     ; t23

    mova                 m4, [tmpq+21*%%str] ; t19
    VP9_UNPACK_MULSUB_2W_4X  10,  5, 15137,  6270, [pd_8192], 0, 7 ; t18, t29
    VP9_UNPACK_MULSUB_2W_4X  11,  4, 15137,  6270, [pd_8192], 0, 7 ; t19, t28
    VP9_UNPACK_MULSUB_2W_4X   3, 12, 6270, m15137, [pd_8192], 0, 7 ; t20, t27
    VP9_UNPACK_MULSUB_2W_4X   2, 13, 6270, m15137, [pd_8192], 0, 7 ; t21, t26

    ; m8=t16, m9=t17, m10=t18, m11=t19, m3=t20, m2=t21, m1=t22, m0=t23,
    ; m15=t24, m14=t25, m13=t26, m12=t27, m4=t28, m5=t29, m6=t30, m7=t31

    SUMSUB_BA             w,  1,  9,  0
    SUMSUB_BA             w,  2, 10,  0
    SUMSUB_BA             w,  3, 11,  0
    SUMSUB_BA             w, 12,  4,  0
    SUMSUB_BA             w, 13,  5,  0
    SUMSUB_BA             w, 14,  6,  0

    ; m0=t16, m1=t17, m2=t18, m3=t19, m11=t20, m10=t21, m9=t22, m8=t23,
    ; m7=t24, m6=t25, m5=t26, m4=t27, m12=t28, m13=t29, m14=t30, m15=t31

    SUMSUB_BA             w,  9,  6,  0
    SUMSUB_BA             w, 10,  5,  0
    SUMSUB_BA             w, 11,  4,  0

    pmulhrsw             m6, [pw_11585x2]
    pmulhrsw             m9, [pw_11585x2]
    pmulhrsw             m5, [pw_11585x2]
    pmulhrsw            m10, [pw_11585x2]
    pmulhrsw             m4, [pw_11585x2]
    pmulhrsw            m11, [pw_11585x2]

    ; m0=t16, m1=t17, m2=t18, m3=t19, m4=t20, m5=t21, m6=t22, m7=t23,
    ; m8=t24, m9=t25, m10=t26, m11=t27, m12=t28, m13=t29, m14=t30, m15=t31

    ; store t17-19 (and t20-22 for pass 1) - keep t24-31 in registers for
    ; final sumsub in pass 1, or keep t20-22 and t24-31 in registers for
    ; final sumsub of pass 2
    mova    [tmpq+ 5*%%str], m1     ; t17
    mova    [tmpq+ 9*%%str], m2     ; t18
    mova    [tmpq+13*%%str], m3     ; t19

    ; then do final pass to sumsub+store the two halves
%if %2 == 1
    mova    [tmpq+17*%%str], m4     ; t20
    mova    [tmpq+21*%%str], m5     ; t21
    mova    [tmpq+25*%%str], m6     ; t22

    mova                 m0, [tmpq+ 0*%%str] ; t0
    mova                 m1, [tmpq+ 4*%%str] ; t1
    mova                 m2, [tmpq+ 8*%%str] ; t2
    mova                 m3, [tmpq+12*%%str] ; t3
    mova                 m4, [tmpq+16*%%str] ; t4
    mova                 m5, [tmpq+20*%%str] ; t5
    mova                 m6, [tmpq+24*%%str] ; t6

    SUMSUB_BA             w, 15,  0, 7
    mova    [tmpq+ 3*%%str], m0              ; t15
    mova                 m7, [tmpq+28*%%str] ; t7
    SUMSUB_BA             w, 14,  1, 0
    SUMSUB_BA             w, 13,  2, 0
    SUMSUB_BA             w, 12,  3, 0
    SUMSUB_BA             w, 11,  4, 0
    SUMSUB_BA             w, 10,  5, 0
    SUMSUB_BA             w,  9,  6, 0
    SUMSUB_BA             w,  8,  7, 0

    TRANSPOSE8x8W        15, 14, 13, 12, 11, 10, 9, 8, 0
    mova    [tmpq+ 0*%%str], m15
    mova    [tmpq+ 4*%%str], m14
    mova    [tmpq+ 8*%%str], m13
    mova    [tmpq+12*%%str], m12
    mova    [tmpq+16*%%str], m11
    mova    [tmpq+20*%%str], m10
    mova    [tmpq+24*%%str], m9
    mova    [tmpq+28*%%str], m8

    mova                  m0, [tmpq+ 3*%%str] ; t15
    TRANSPOSE8x8W          7, 6, 5, 4, 3, 2, 1, 0, 8
    mova    [tmpq+ 3*%%str], m7
    mova    [tmpq+ 7*%%str], m6
    mova    [tmpq+11*%%str], m5
    mova    [tmpq+15*%%str], m4
    mova    [tmpq+19*%%str], m3
    mova    [tmpq+23*%%str], m2
    mova    [tmpq+27*%%str], m1
    mova    [tmpq+31*%%str], m0

    mova                m15, [tmpq+ 2*%%str] ; t8
    mova                m14, [tmpq+ 6*%%str] ; t9
    mova                m13, [tmpq+10*%%str] ; t10
    mova                m12, [tmpq+14*%%str] ; t11
    mova                m11, [tmpq+18*%%str] ; t12
    mova                m10, [tmpq+22*%%str] ; t13
    mova                 m9, [tmpq+26*%%str] ; t14
    mova                 m8, [tmpq+30*%%str] ; t15
    mova                 m7, [tmpq+ 1*%%str] ; t16
    mova                 m6, [tmpq+ 5*%%str] ; t17
    mova                 m5, [tmpq+ 9*%%str] ; t18
    mova                 m4, [tmpq+13*%%str] ; t19
    mova                 m3, [tmpq+17*%%str] ; t20
    mova                 m2, [tmpq+21*%%str] ; t21
    mova                 m1, [tmpq+25*%%str] ; t22

    SUMSUB_BA             w,  7,  8, 0
    mova    [tmpq+ 2*%%str], m8
    mova                 m0, [tmpq+29*%%str] ; t23
    SUMSUB_BA             w,  6,  9, 8
    SUMSUB_BA             w,  5, 10, 8
    SUMSUB_BA             w,  4, 11, 8
    SUMSUB_BA             w,  3, 12, 8
    SUMSUB_BA             w,  2, 13, 8
    SUMSUB_BA             w,  1, 14, 8
    SUMSUB_BA             w,  0, 15, 8

    TRANSPOSE8x8W         0, 1, 2, 3, 4, 5, 6, 7, 8
    mova    [tmpq+ 1*%%str], m0
    mova    [tmpq+ 5*%%str], m1
    mova    [tmpq+ 9*%%str], m2
    mova    [tmpq+13*%%str], m3
    mova    [tmpq+17*%%str], m4
    mova    [tmpq+21*%%str], m5
    mova    [tmpq+25*%%str], m6
    mova    [tmpq+29*%%str], m7

    mova                 m8, [tmpq+ 2*%%str]
    TRANSPOSE8x8W         8, 9, 10, 11, 12, 13, 14, 15, 0
    mova    [tmpq+ 2*%%str], m8
    mova    [tmpq+ 6*%%str], m9
    mova    [tmpq+10*%%str], m10
    mova    [tmpq+14*%%str], m11
    mova    [tmpq+18*%%str], m12
    mova    [tmpq+22*%%str], m13
    mova    [tmpq+26*%%str], m14
    mova    [tmpq+30*%%str], m15
%else
    ; t0-7 is in [tmpq+{0,4,8,12,16,20,24,28}*%%str]
    ; t8-15 is in [tmpq+{2,6,10,14,18,22,26,30}*%%str]
    ; t16-19 and t23 is in [tmpq+{1,5,9,13,29}*%%str]
    ; t20-22 is in m4-6
    ; t24-31 is in m8-15
    pxor                m7, m7

%macro %%STORE_2X2 7-8 1 ; src[1-4], tmp[1-2], zero, inc_dst_ptrs
    SUMSUB_BA            w, %4, %1, %5
    SUMSUB_BA            w, %3, %2, %5
    pmulhrsw           m%4, [pw_512]
    pmulhrsw           m%3, [pw_512]
    VP9_STORE_2X        %4, %3, %5, %6, %7
%if %8 == 1
    add               dstq, stride2q
%endif
    pmulhrsw           m%2, [pw_512]
    pmulhrsw           m%1, [pw_512]
    VP9_STORE_2X        %2, %1, %5, %6, %7, dst_endq
%if %8 == 1
    sub           dst_endq, stride2q
%endif
%endmacro

    ; store t0-1 and t30-31
    mova                m0, [tmpq+ 0*%%str]
    mova                m1, [tmpq+ 4*%%str]
    %%STORE_2X2          0,  1, 14, 15, 2, 3, 7

    ; store t2-3 and t28-29
    mova                m0, [tmpq+ 8*%%str]
    mova                m1, [tmpq+12*%%str]
    %%STORE_2X2          0,  1, 12, 13, 2, 3, 7

    ; store t4-5 and t26-27
    mova                m0, [tmpq+16*%%str]
    mova                m1, [tmpq+20*%%str]
    %%STORE_2X2          0,  1, 10, 11, 2, 3, 7

    ; store t6-7 and t24-25
    mova                m0, [tmpq+24*%%str]
    mova                m1, [tmpq+28*%%str]
    %%STORE_2X2          0,  1,  8,  9, 2, 3, 7

    ; store t8-9 and t22-23
    mova                m0, [tmpq+ 2*%%str]
    mova                m1, [tmpq+ 6*%%str]
    mova                m8, [tmpq+29*%%str]
    %%STORE_2X2          0,  1,  6,  8, 2, 3, 7

    ; store t10-11 and t20-21
    mova                m0, [tmpq+10*%%str]
    mova                m1, [tmpq+14*%%str]
    %%STORE_2X2          0,  1,  4,  5, 2, 3, 7

    ; store t12-13 and t18-19
    mova                m0, [tmpq+18*%%str]
    mova                m1, [tmpq+22*%%str]
    mova                m5, [tmpq+13*%%str]
    mova                m4, [tmpq+ 9*%%str]
    %%STORE_2X2          0,  1,  4,  5, 2, 3, 7

    ; store t14-17
    mova                m0, [tmpq+26*%%str]
    mova                m1, [tmpq+30*%%str]
    mova                m5, [tmpq+ 5*%%str]
    mova                m4, [tmpq+ 1*%%str]
    %%STORE_2X2          0,  1,  4,  5, 2, 3, 7, 0
%endif
%endmacro

%macro VP9_IDCT_IDCT_32x32_ADD_XMM 1
INIT_XMM %1
cglobal vp9_idct_idct_32x32_add, 4, 9, 16, 2048, dst, stride, block, eob
    cmp eobd, 135
    jg .idctfull
    cmp eobd, 34
    jg .idct16x16
    cmp eobd, 1
    jg .idct8x8

    ; dc-only case
    movd                m0, [blockq]
    mova                m1, [pw_11585x2]
    pmulhrsw            m0, m1
    pmulhrsw            m0, m1
    SPLATW              m0, m0, q0000
    pmulhrsw            m0, [pw_512]
    pxor                m5, m5
    movd          [blockq], m5
    DEFINE_ARGS        dst, stride, block, cnt
%rep 31
    VP9_STORE_2XFULL    0, 1, 2, 3, 4, 5, mmsize
    add               dstq, strideq
%endrep
    VP9_STORE_2XFULL    0, 1, 2, 3, 4, 5, mmsize
    RET

    DEFINE_ARGS dst_bak, stride, block, cnt, dst, stride30, dst_end, stride2, tmp
.idct8x8:
    mov               tmpq, rsp
    VP9_IDCT32_1D   blockq, 1, 8

    mov          stride30q, strideq         ; stride
    lea           stride2q, [strideq*2]     ; stride*2
    shl          stride30q, 5               ; stride*32
    mov               cntd, 4
    sub          stride30q, stride2q        ; stride*30
.loop2_8x8:
    mov               dstq, dst_bakq
    lea           dst_endq, [dst_bakq+stride30q]
    VP9_IDCT32_1D     tmpq, 2, 8
    add           dst_bakq, 8
    add               tmpq, 16
    dec               cntd
    jg .loop2_8x8

    ; at the end of the loop, m7 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 64,  8, m7
    RET

.idct16x16:
    mov               cntd, 2
    mov               tmpq, rsp
.loop1_16x16:
    VP9_IDCT32_1D   blockq, 1, 16
    add             blockq, 16
    add               tmpq, 512
    dec               cntd
    jg .loop1_16x16
    sub             blockq, 32

    mov          stride30q, strideq         ; stride
    lea           stride2q, [strideq*2]     ; stride*2
    shl          stride30q, 5               ; stride*32
    mov               cntd, 4
    mov               tmpq, rsp
    sub          stride30q, stride2q        ; stride*30
.loop2_16x16:
    mov               dstq, dst_bakq
    lea           dst_endq, [dst_bakq+stride30q]
    VP9_IDCT32_1D     tmpq, 2, 16
    add           dst_bakq, 8
    add               tmpq, 16
    dec               cntd
    jg .loop2_16x16

    ; at the end of the loop, m7 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 64, 16, m7
    RET

.idctfull:
    mov               cntd, 4
    mov               tmpq, rsp
.loop1_full:
    VP9_IDCT32_1D   blockq, 1
    add             blockq, 16
    add               tmpq, 512
    dec               cntd
    jg .loop1_full
    sub             blockq, 64

    mov          stride30q, strideq         ; stride
    lea           stride2q, [strideq*2]     ; stride*2
    shl          stride30q, 5               ; stride*32
    mov               cntd, 4
    mov               tmpq, rsp
    sub          stride30q, stride2q        ; stride*30
.loop2_full:
    mov               dstq, dst_bakq
    lea           dst_endq, [dst_bakq+stride30q]
    VP9_IDCT32_1D     tmpq, 2
    add           dst_bakq, 8
    add               tmpq, 16
    dec               cntd
    jg .loop2_full

    ; at the end of the loop, m7 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 64, 32, m7
    RET
%endmacro

VP9_IDCT_IDCT_32x32_ADD_XMM ssse3
VP9_IDCT_IDCT_32x32_ADD_XMM avx

%endif ; x86-64
