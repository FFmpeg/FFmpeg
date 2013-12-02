;******************************************************************************
;* VP9 IDCT SIMD optimizations
;*
;* Copyright (C) 2013 Clément Bœsch <u pkh me>
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
pw_m%1_%2: dw -%1, %2, -%1, %2, -%1, %2, -%1, %2
pw_%2_%1:  dw  %2, %1,  %2, %1,  %2, %1,  %2, %1
%endmacro

%macro VP9_IDCT_COEFFS_ALL 2
pw_%1x2: times 8 dw %1*2
pw_%2x2: times 8 dw %2*2
VP9_IDCT_COEFFS %1, %2
%endmacro

VP9_IDCT_COEFFS_ALL 15137,  6270
VP9_IDCT_COEFFS_ALL 16069,  3196
VP9_IDCT_COEFFS_ALL  9102, 13623

pd_8192: times 4 dd 8192
pw_2048: times 8 dw 2048
pw_1024: times 8 dw 1024

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

;-------------------------------------------------------------------------------------------
; void vp9_idct_idct_8x8_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;-------------------------------------------------------------------------------------------

%if ARCH_X86_64 ; TODO: 32-bit? (32-bit limited to 8 xmm reg, we use 13 here)
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
%endif
