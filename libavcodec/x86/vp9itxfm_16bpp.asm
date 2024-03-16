;******************************************************************************
;* VP9 inverse transform x86 SIMD optimizations
;*
;* Copyright (C) 2015 Ronald S. Bultje <rsbultje gmail com>
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
%include "vp9itxfm_template.asm"

SECTION_RODATA

cextern pw_8
cextern pw_1023
cextern pw_2048
cextern pw_4095
cextern pw_m1
cextern pd_1
cextern pd_16
cextern pd_32
cextern pd_8192

pd_8: times 4 dd 8
pd_3fff: times 4 dd 0x3fff

cextern pw_11585x2

cextern pw_5283_13377
cextern pw_9929_13377
cextern pw_15212_m13377
cextern pw_15212_9929
cextern pw_m5283_m15212
cextern pw_13377x2
cextern pw_m13377_13377
cextern pw_13377_0

pw_9929_m5283: times 4 dw 9929, -5283

%macro COEF_PAIR 2-3
cextern pw_m%1_%2
cextern pw_%2_%1
%if %0 == 3
cextern pw_m%1_m%2
%if %1 != %2
cextern pw_m%2_%1
cextern pw_%1_%2
%endif
%endif
%endmacro

COEF_PAIR  2404, 16207
COEF_PAIR  3196, 16069, 1
COEF_PAIR  4756, 15679
COEF_PAIR  5520, 15426
COEF_PAIR  6270, 15137, 1
COEF_PAIR  8423, 14053
COEF_PAIR 10394, 12665
COEF_PAIR 11003, 12140
COEF_PAIR 11585, 11585, 1
COEF_PAIR 13160,  9760
COEF_PAIR 13623,  9102, 1
COEF_PAIR 14449,  7723
COEF_PAIR 14811,  7005
COEF_PAIR 15893,  3981
COEF_PAIR 16305,  1606
COEF_PAIR 16364,   804

default_8x8:
times 12 db 1
times 52 db 2
row_8x8:
times 18 db 1
times 46 db 2
col_8x8:
times 6 db 1
times 58 db 2
default_16x16:
times 10 db 1
times 28 db 2
times 51 db 3
times 167 db 4
row_16x16:
times 21 db 1
times 45 db 2
times 60 db 3
times 130 db 4
col_16x16:
times 5 db 1
times 12 db 2
times 25 db 3
times 214 db 4
default_32x32:
times 9 db 1
times 25 db 2
times 36 db 3
times 65 db 4
times 105 db 5
times 96 db 6
times 112 db 7
times 576 db 8

SECTION .text

%macro VP9_STORE_2X 6-7 dstq ; reg1, reg2, tmp1, tmp2, min, max, dst
    mova               m%3, [%7]
    mova               m%4, [%7+strideq]
    paddw              m%3, m%1
    paddw              m%4, m%2
    pmaxsw             m%3, m%5
    pmaxsw             m%4, m%5
    pminsw             m%3, m%6
    pminsw             m%4, m%6
    mova              [%7], m%3
    mova      [%7+strideq], m%4
%endmacro

%macro ZERO_BLOCK 4 ; mem, stride, nnzcpl, zero_reg
%assign %%y 0
%rep %3
%assign %%x 0
%rep %3*4/mmsize
    mova      [%1+%%y+%%x], %4
%assign %%x (%%x+mmsize)
%endrep
%assign %%y (%%y+%2)
%endrep
%endmacro

; the input coefficients are scaled up by 2 bit (which we downscale immediately
; in the iwht), and is otherwise orthonormally increased by 1 bit per iwht_1d.
; therefore, a diff of 10-12+sign bit will fit in 12-14+sign bit after scaling,
; i.e. everything can be done in 15+1bpp words. Since the quant fractional bits
; add 2 bits, we need to scale before converting to word in 12bpp, since the
; input will be 16+sign bit which doesn't fit in 15+sign words, but in 10bpp
; we can scale after converting to words (which is half the instructions),
; since the input is only 14+sign bit, which fits in 15+sign words directly.

%macro IWHT4_FN 2 ; bpp, max
cglobal vp9_iwht_iwht_4x4_add_%1, 3, 3, 8, dst, stride, block, eob
    mova                m7, [pw_%2]
    mova                m0, [blockq+0*16+0]
    mova                m1, [blockq+1*16+0]
%if %1 >= 12
    mova                m4, [blockq+0*16+8]
    mova                m5, [blockq+1*16+8]
    psrad               m0, 2
    psrad               m1, 2
    psrad               m4, 2
    psrad               m5, 2
    packssdw            m0, m4
    packssdw            m1, m5
%else
    packssdw            m0, [blockq+0*16+8]
    packssdw            m1, [blockq+1*16+8]
    psraw               m0, 2
    psraw               m1, 2
%endif
    mova                m2, [blockq+2*16+0]
    mova                m3, [blockq+3*16+0]
%if %1 >= 12
    mova                m4, [blockq+2*16+8]
    mova                m5, [blockq+3*16+8]
    psrad               m2, 2
    psrad               m3, 2
    psrad               m4, 2
    psrad               m5, 2
    packssdw            m2, m4
    packssdw            m3, m5
%else
    packssdw            m2, [blockq+2*16+8]
    packssdw            m3, [blockq+3*16+8]
    psraw               m2, 2
    psraw               m3, 2
%endif

    VP9_IWHT4_1D
    TRANSPOSE4x4W        0, 1, 2, 3, 4
    VP9_IWHT4_1D

    pxor                m6, m6
    VP9_STORE_2X         0, 1, 4, 5, 6, 7
    lea               dstq, [dstq+strideq*2]
    VP9_STORE_2X         2, 3, 4, 5, 6, 7
    ZERO_BLOCK      blockq, 16, 4, m6
    RET
%endmacro

INIT_MMX mmxext
IWHT4_FN 10, 1023
INIT_MMX mmxext
IWHT4_FN 12, 4095

%macro VP9_IDCT4_WRITEOUT 0
%if cpuflag(ssse3)
    mova                m5, [pw_2048]
    pmulhrsw            m0, m5
    pmulhrsw            m1, m5
    pmulhrsw            m2, m5
    pmulhrsw            m3, m5
%else
    mova                m5, [pw_8]
    paddw               m0, m5
    paddw               m1, m5
    paddw               m2, m5
    paddw               m3, m5
    psraw               m0, 4
    psraw               m1, 4
    psraw               m2, 4
    psraw               m3, 4
%endif
    mova                m5, [pw_1023]
    VP9_STORE_2X         0,  1,  6,  7,  4,  5
    lea               dstq, [dstq+2*strideq]
    VP9_STORE_2X         2,  3,  6,  7,  4,  5
%endmacro

%macro DC_ONLY 2 ; shift, zero
    mov              coefd, dword [blockq]
    movd          [blockq], %2
    imul             coefd, 11585
    add              coefd, 8192
    sar              coefd, 14
    imul             coefd, 11585
    add              coefd, ((1 << (%1 - 1)) << 14) + 8192
    sar              coefd, 14 + %1
%endmacro

; 4x4 coefficients are 5+depth+sign bits, so for 10bpp, everything still fits
; in 15+1 words without additional effort, since the coefficients are 15bpp.

%macro IDCT4_10_FN 0
cglobal vp9_idct_idct_4x4_add_10, 4, 4, 8, dst, stride, block, eob
    cmp               eobd, 1
    jg .idctfull

    ; dc-only
    pxor                m4, m4
%if cpuflag(ssse3)
    movd                m0, [blockq]
    movd          [blockq], m4
    mova                m5, [pw_11585x2]
    pmulhrsw            m0, m5
    pmulhrsw            m0, m5
%else
    DEFINE_ARGS dst, stride, block, coef
    DC_ONLY              4, m4
    movd                m0, coefd
%endif
    pshufw              m0, m0, 0
    mova                m5, [pw_1023]
%if cpuflag(ssse3)
    pmulhrsw            m0, [pw_2048]       ; (x*2048 + (1<<14))>>15 <=> (x+8)>>4
%endif
    VP9_STORE_2X         0,  0,  6,  7,  4,  5
    lea               dstq, [dstq+2*strideq]
    VP9_STORE_2X         0,  0,  6,  7,  4,  5
    RET

.idctfull:
    mova                m0, [blockq+0*16+0]
    mova                m1, [blockq+1*16+0]
    packssdw            m0, [blockq+0*16+8]
    packssdw            m1, [blockq+1*16+8]
    mova                m2, [blockq+2*16+0]
    mova                m3, [blockq+3*16+0]
    packssdw            m2, [blockq+2*16+8]
    packssdw            m3, [blockq+3*16+8]

%if cpuflag(ssse3)
    mova                m6, [pw_11585x2]
%endif
    mova                m7, [pd_8192]       ; rounding
    VP9_IDCT4_1D
    TRANSPOSE4x4W  0, 1, 2, 3, 4
    VP9_IDCT4_1D

    pxor                m4, m4
    ZERO_BLOCK      blockq, 16, 4, m4
    VP9_IDCT4_WRITEOUT
    RET
%endmacro

INIT_MMX mmxext
IDCT4_10_FN
INIT_MMX ssse3
IDCT4_10_FN

%macro IADST4_FN 4
cglobal vp9_%1_%3_4x4_add_10, 3, 3, 0, dst, stride, block, eob
%if WIN64 && notcpuflag(ssse3)
INIT_XMM cpuname
    WIN64_SPILL_XMM 8
INIT_MMX cpuname
%endif
    movdqa            xmm5, [pd_8192]
    mova                m0, [blockq+0*16+0]
    mova                m1, [blockq+1*16+0]
    packssdw            m0, [blockq+0*16+8]
    packssdw            m1, [blockq+1*16+8]
    mova                m2, [blockq+2*16+0]
    mova                m3, [blockq+3*16+0]
    packssdw            m2, [blockq+2*16+8]
    packssdw            m3, [blockq+3*16+8]

%if cpuflag(ssse3)
    mova                m6, [pw_11585x2]
%endif
%ifnidn %1%3, iadstiadst
    movdq2q             m7, xmm5
%endif
    VP9_%2_1D
    TRANSPOSE4x4W  0, 1, 2, 3, 4
    VP9_%4_1D

    pxor                m4, m4
    ZERO_BLOCK      blockq, 16, 4, m4
    VP9_IDCT4_WRITEOUT
    RET
%endmacro

INIT_MMX sse2
IADST4_FN idct,  IDCT4,  iadst, IADST4
IADST4_FN iadst, IADST4, idct,  IDCT4
IADST4_FN iadst, IADST4, iadst, IADST4

INIT_MMX ssse3
IADST4_FN idct,  IDCT4,  iadst, IADST4
IADST4_FN iadst, IADST4, idct,  IDCT4
IADST4_FN iadst, IADST4, iadst, IADST4

; inputs and outputs are dwords, coefficients are words
;
; dst1 = src1 * coef1 + src2 * coef2 + rnd >> 14
; dst2 = src1 * coef2 - src2 * coef1 + rnd >> 14
%macro SUMSUB_MUL 6-8 [pd_8192], [pd_3fff] ; src/dst 1-2, tmp1-2, coef1-2, rnd, mask
    pand               m%3, m%1, %8
    pand               m%4, m%2, %8
    psrad              m%1, 14
    psrad              m%2, 14
    packssdw           m%4, m%2
    packssdw           m%3, m%1
    punpckhwd          m%2, m%4, m%3
    punpcklwd          m%4, m%3
    pmaddwd            m%3, m%4, [pw_%6_%5]
    pmaddwd            m%1, m%2, [pw_%6_%5]
    pmaddwd            m%4, [pw_m%5_%6]
    pmaddwd            m%2, [pw_m%5_%6]
    paddd              m%3, %7
    paddd              m%4, %7
    psrad              m%3, 14
    psrad              m%4, 14
    paddd              m%1, m%3
    paddd              m%2, m%4
%endmacro

%macro IDCT4_12BPP_1D 0-8 [pd_8192], [pd_3fff], 0, 1, 2, 3, 4, 5 ; rnd, mask, in/out0-3, tmp0-1
    SUMSUB_MUL          %3, %5, %7, %8, 11585, 11585, %1, %2
    SUMSUB_MUL          %4, %6, %7, %8, 15137,  6270, %1, %2
    SUMSUB_BA        d, %4, %3, %7
    SUMSUB_BA        d, %6, %5, %7
    SWAP                %4, %6, %3
%endmacro

%macro STORE_4x4 6 ; tmp1-2, reg1-2, min, max
    movh               m%1, [dstq+strideq*0]
    movh               m%2, [dstq+strideq*2]
    movhps             m%1, [dstq+strideq*1]
    movhps             m%2, [dstq+stride3q ]
    paddw              m%1, m%3
    paddw              m%2, m%4
    pmaxsw             m%1, %5
    pmaxsw             m%2, %5
    pminsw             m%1, %6
    pminsw             m%2, %6
    movh   [dstq+strideq*0], m%1
    movhps [dstq+strideq*1], m%1
    movh   [dstq+strideq*2], m%2
    movhps [dstq+stride3q ], m%2
%endmacro

%macro ROUND_AND_STORE_4x4 8 ; reg1-4, min, max, rnd, shift
    paddd              m%1, %7
    paddd              m%2, %7
    paddd              m%3, %7
    paddd              m%4, %7
    psrad              m%1, %8
    psrad              m%2, %8
    psrad              m%3, %8
    psrad              m%4, %8
    packssdw           m%1, m%2
    packssdw           m%3, m%4
    STORE_4x4           %2, %4, %1, %3, %5, %6
%endmacro

INIT_XMM sse2
cglobal vp9_idct_idct_4x4_add_12, 4, 4, 8, dst, stride, block, eob
    cmp               eobd, 1
    jg .idctfull

    ; dc-only - this is special, since for 4x4 12bpp, the max coef size is
    ; 17+sign bpp. Since the multiply is with 11585, which is 14bpp, the
    ; result of each multiply is 31+sign bit, i.e. it _exactly_ fits in a
    ; dword. After the final shift (4), the result is 13+sign bits, so we
    ; don't need any additional processing to fit it in a word
    DEFINE_ARGS dst, stride, block, coef
    pxor                m4, m4
    DC_ONLY              4, m4
    movd                m0, coefd
    pshuflw             m0, m0, q0000
    punpcklqdq          m0, m0
    mova                m5, [pw_4095]
    DEFINE_ARGS dst, stride, stride3
    lea           stride3q, [strideq*3]
    STORE_4x4            1, 3, 0, 0, m4, m5
    RET

.idctfull:
    DEFINE_ARGS dst, stride, block, eob
    mova                m0, [blockq+0*16]
    mova                m1, [blockq+1*16]
    mova                m2, [blockq+2*16]
    mova                m3, [blockq+3*16]
    mova                m6, [pd_8192]
    mova                m7, [pd_3fff]

    IDCT4_12BPP_1D      m6, m7
    TRANSPOSE4x4D        0, 1, 2, 3, 4
    IDCT4_12BPP_1D      m6, m7

    pxor                m4, m4
    ZERO_BLOCK      blockq, 16, 4, m4

    ; writeout
    DEFINE_ARGS dst, stride, stride3
    lea           stride3q, [strideq*3]
    mova                m5, [pw_4095]
    mova                m6, [pd_8]
    ROUND_AND_STORE_4x4  0, 1, 2, 3, m4, m5, m6, 4
    RET

%macro SCRATCH 3-4
%if ARCH_X86_64
    SWAP                %1, %2
%if %0 == 4
%define reg_%4 m%2
%endif
%else
    mova              [%3], m%1
%if %0 == 4
%define reg_%4 [%3]
%endif
%endif
%endmacro

%macro UNSCRATCH 3-4
%if ARCH_X86_64
    SWAP                %1, %2
%else
    mova               m%1, [%3]
%endif
%if %0 == 4
%undef reg_%4
%endif
%endmacro

%macro PRELOAD 2-3
%if ARCH_X86_64
    mova               m%1, [%2]
%if %0 == 3
%define reg_%3 m%1
%endif
%elif %0 == 3
%define reg_%3 [%2]
%endif
%endmacro

; out0 =  5283 * in0 + 13377 + in1 + 15212 * in2 +  9929 * in3 + rnd >> 14
; out1 =  9929 * in0 + 13377 * in1 -  5283 * in2 - 15282 * in3 + rnd >> 14
; out2 = 13377 * in0               - 13377 * in2 + 13377 * in3 + rnd >> 14
; out3 = 15212 * in0 - 13377 * in1 +  9929 * in2 -  5283 * in3 + rnd >> 14
%macro IADST4_12BPP_1D 0-2 [pd_8192], [pd_3fff] ; rnd, mask
    pand                m4, m0, %2
    pand                m5, m1, %2
    psrad               m0, 14
    psrad               m1, 14
    packssdw            m5, m1
    packssdw            m4, m0
    punpckhwd           m1, m4, m5
    punpcklwd           m4, m5
    pand                m5, m2, %2
    pand                m6, m3, %2
    psrad               m2, 14
    psrad               m3, 14
    packssdw            m6, m3
    packssdw            m5, m2
    punpckhwd           m3, m5, m6
    punpcklwd           m5, m6
    SCRATCH              1,  8, rsp+0*mmsize, a
    SCRATCH              5,  9, rsp+1*mmsize, b

    ; m1/3 have the high bits of 0,1,2,3
    ; m4/5 have the low bits of 0,1,2,3
    ; m0/2/6/7 are free

    mova                m2, [pw_15212_9929]
    mova                m0, [pw_5283_13377]
    pmaddwd             m7, m2, reg_b
    pmaddwd             m6, m4, m0
    pmaddwd             m2, m3
    pmaddwd             m0, reg_a
    paddd               m6, m7
    paddd               m0, m2
    mova                m1, [pw_m13377_13377]
    mova                m5, [pw_13377_0]
    pmaddwd             m7, m1, reg_b
    pmaddwd             m2, m4, m5
    pmaddwd             m1, m3
    pmaddwd             m5, reg_a
    paddd               m2, m7
    paddd               m1, m5
    paddd               m6, %1
    paddd               m2, %1
    psrad               m6, 14
    psrad               m2, 14
    paddd               m0, m6                      ; t0
    paddd               m2, m1                      ; t2

    mova                m7, [pw_m5283_m15212]
    mova                m5, [pw_9929_13377]
    pmaddwd             m1, m7, reg_b
    pmaddwd             m6, m4, m5
    pmaddwd             m7, m3
    pmaddwd             m5, reg_a
    paddd               m6, m1
    paddd               m7, m5
    UNSCRATCH            5,  9, rsp+1*mmsize, b
    pmaddwd             m5, [pw_9929_m5283]
    pmaddwd             m4, [pw_15212_m13377]
    pmaddwd             m3, [pw_9929_m5283]
    UNSCRATCH            1,  8, rsp+0*mmsize, a
    pmaddwd             m1, [pw_15212_m13377]
    paddd               m4, m5
    paddd               m3, m1
    paddd               m6, %1
    paddd               m4, %1
    psrad               m6, 14
    psrad               m4, 14
    paddd               m7, m6                      ; t1
    paddd               m3, m4                      ; t3

    SWAP                 1, 7
%endmacro

%macro IADST4_12BPP_FN 4
cglobal vp9_%1_%3_4x4_add_12, 3, 3, 12, 2 * ARCH_X86_32 * mmsize, dst, stride, block, eob
    mova                m0, [blockq+0*16]
    mova                m1, [blockq+1*16]
    mova                m2, [blockq+2*16]
    mova                m3, [blockq+3*16]

    PRELOAD             10, pd_8192, rnd
    PRELOAD             11, pd_3fff, mask
    %2_12BPP_1D    reg_rnd, reg_mask
    TRANSPOSE4x4D        0, 1, 2, 3, 4
    %4_12BPP_1D    reg_rnd, reg_mask

    pxor                m4, m4
    ZERO_BLOCK      blockq, 16, 4, m4

    ; writeout
    DEFINE_ARGS dst, stride, stride3
    lea           stride3q, [strideq*3]
    mova                m5, [pw_4095]
    mova                m6, [pd_8]
    ROUND_AND_STORE_4x4  0, 1, 2, 3, m4, m5, m6, 4
    RET
%endmacro

INIT_XMM sse2
IADST4_12BPP_FN idct,  IDCT4,  iadst, IADST4
IADST4_12BPP_FN iadst, IADST4, idct,  IDCT4
IADST4_12BPP_FN iadst, IADST4, iadst, IADST4

; the following line has not been executed at the end of this macro:
; UNSCRATCH            6, 8, rsp+%3*mmsize
%macro IDCT8_1D 1-5 [pd_8192], [pd_3fff], 2 * mmsize, 17 ; src, rnd, mask, src_stride, stack_offset
    mova                m0, [%1+0*%4]
    mova                m2, [%1+2*%4]
    mova                m4, [%1+4*%4]
    mova                m6, [%1+6*%4]
    IDCT4_12BPP_1D      %2, %3, 0, 2, 4, 6, 1, 3            ; m0/2/4/6 have t0/1/2/3
    SCRATCH              4, 8, rsp+(%5+0)*mmsize
    SCRATCH              6, 9, rsp+(%5+1)*mmsize
    mova                m1, [%1+1*%4]
    mova                m3, [%1+3*%4]
    mova                m5, [%1+5*%4]
    mova                m7, [%1+7*%4]
    SUMSUB_MUL           1, 7, 4, 6, 16069,  3196, %2, %3   ; m1=t7a, m7=t4a
    SUMSUB_MUL           5, 3, 4, 6,  9102, 13623, %2, %3   ; m5=t6a, m3=t5a
    SUMSUB_BA         d, 3, 7, 4                            ; m3=t4, m7=t5a
    SUMSUB_BA         d, 5, 1, 4                            ; m5=t7, m1=t6a
    SUMSUB_MUL           1, 7, 4, 6, 11585, 11585, %2, %3   ; m1=t6, m7=t5
    SUMSUB_BA         d, 5, 0, 4                            ; m5=out0, m0=out7
    SUMSUB_BA         d, 1, 2, 4                            ; m1=out1, m2=out6
    UNSCRATCH            4, 8, rsp+(%5+0)*mmsize
    UNSCRATCH            6, 9, rsp+(%5+1)*mmsize
    SCRATCH              2, 8, rsp+(%5+0)*mmsize
    SUMSUB_BA         d, 7, 4, 2                            ; m7=out2, m4=out5
    SUMSUB_BA         d, 3, 6, 2                            ; m3=out3, m6=out4
    SWAP                 0, 5, 4, 6, 2, 7
%endmacro

%macro STORE_2x8 5-7 dstq, strideq ; tmp1-2, reg, min, max
    mova               m%1, [%6+%7*0]
    mova               m%2, [%6+%7*1]
    paddw              m%1, m%3
    paddw              m%2, m%3
    pmaxsw             m%1, %4
    pmaxsw             m%2, %4
    pminsw             m%1, %5
    pminsw             m%2, %5
    mova         [%6+%7*0], m%1
    mova         [%6+%7*1], m%2
%endmacro

; FIXME we can use the intermediate storage (rsp[0-15]) on x86-32 for temp
; storage also instead of allocating two more stack spaces. This doesn't
; matter much but it's something...
INIT_XMM sse2
cglobal vp9_idct_idct_8x8_add_10, 4, 6 + ARCH_X86_64, 14, \
                                  16 * mmsize + 3 * ARCH_X86_32 * mmsize, \
                                  dst, stride, block, eob
    mova                m0, [pw_1023]
    cmp               eobd, 1
    jg .idctfull

    ; dc-only - the 10bit version can be done entirely in 32bit, since the max
    ; coef values are 16+sign bit, and the coef is 14bit, so 30+sign easily
    ; fits in 32bit
    DEFINE_ARGS dst, stride, block, coef
    pxor                m2, m2
    DC_ONLY              5, m2
    movd                m1, coefd
    pshuflw             m1, m1, q0000
    punpcklqdq          m1, m1
    DEFINE_ARGS dst, stride, cnt
    mov               cntd, 4
.loop_dc:
    STORE_2x8            3, 4, 1, m2, m0
    lea               dstq, [dstq+strideq*2]
    dec               cntd
    jg .loop_dc
    RET

.idctfull:
    SCRATCH              0, 12, rsp+16*mmsize, max
    DEFINE_ARGS dst, stride, block, cnt, ptr, skip, dstbak
%if ARCH_X86_64
    mov            dstbakq, dstq
    movsxd            cntq, cntd
%endif
%if PIC
    lea               ptrq, [default_8x8]
    movzx             cntd, byte [ptrq+cntq-1]
%else
    movzx             cntd, byte [default_8x8+cntq-1]
%endif
    mov              skipd, 2
    sub              skipd, cntd
    mov               ptrq, rsp
    PRELOAD             10, pd_8192, rnd
    PRELOAD             11, pd_3fff, mask
    PRELOAD             13, pd_16, srnd
.loop_1:
    IDCT8_1D        blockq, reg_rnd, reg_mask

    TRANSPOSE4x4D        0, 1, 2, 3, 6
    mova  [ptrq+ 0*mmsize], m0
    mova  [ptrq+ 2*mmsize], m1
    mova  [ptrq+ 4*mmsize], m2
    mova  [ptrq+ 6*mmsize], m3
    UNSCRATCH            6, 8, rsp+17*mmsize
    TRANSPOSE4x4D        4, 5, 6, 7, 0
    mova  [ptrq+ 1*mmsize], m4
    mova  [ptrq+ 3*mmsize], m5
    mova  [ptrq+ 5*mmsize], m6
    mova  [ptrq+ 7*mmsize], m7
    add               ptrq, 8 * mmsize
    add             blockq, mmsize
    dec               cntd
    jg .loop_1

    ; zero-pad the remainder (skipped cols)
    test             skipd, skipd
    jz .end
    add              skipd, skipd
    lea             blockq, [blockq+skipq*(mmsize/2)]
    pxor                m0, m0
.loop_z:
    mova   [ptrq+mmsize*0], m0
    mova   [ptrq+mmsize*1], m0
    mova   [ptrq+mmsize*2], m0
    mova   [ptrq+mmsize*3], m0
    add               ptrq, 4 * mmsize
    dec              skipd
    jg .loop_z
.end:

    DEFINE_ARGS dst, stride, block, cnt, ptr, stride3, dstbak
    lea           stride3q, [strideq*3]
    mov               cntd, 2
    mov               ptrq, rsp
.loop_2:
    IDCT8_1D          ptrq, reg_rnd, reg_mask

    pxor                m6, m6
    ROUND_AND_STORE_4x4  0, 1, 2, 3, m6, reg_max, reg_srnd, 5
    lea               dstq, [dstq+strideq*4]
    UNSCRATCH            0, 8, rsp+17*mmsize
    UNSCRATCH            1, 12, rsp+16*mmsize, max
    UNSCRATCH            2, 13, pd_16, srnd
    ROUND_AND_STORE_4x4  4, 5, 0, 7, m6, m1, m2, 5
    add               ptrq, 16
%if ARCH_X86_64
    lea               dstq, [dstbakq+8]
%else
    mov               dstq, dstm
    add               dstq, 8
%endif
    dec               cntd
    jg .loop_2

    ; m6 is still zero
    ZERO_BLOCK blockq-2*mmsize, 32, 8, m6
    RET

%macro DC_ONLY_64BIT 2 ; shift, zero
%if ARCH_X86_64
    movsxd           coefq, dword [blockq]
    movd          [blockq], %2
    imul             coefq, 11585
    add              coefq, 8192
    sar              coefq, 14
    imul             coefq, 11585
    add              coefq, ((1 << (%1 - 1)) << 14) + 8192
    sar              coefq, 14 + %1
%else
    mov              coefd, dword [blockq]
    movd          [blockq], %2
    DEFINE_ARGS dst, stride, cnt, coef, coefl
    mov               cntd, 2
.loop_dc_calc:
    mov             coefld, coefd
    sar              coefd, 14
    and             coefld, 0x3fff
    imul             coefd, 11585
    imul            coefld, 11585
    add             coefld, 8192
    sar             coefld, 14
    add              coefd, coefld
    dec               cntd
    jg .loop_dc_calc
    add              coefd, 1 << (%1 - 1)
    sar              coefd, %1
%endif
%endmacro

INIT_XMM sse2
cglobal vp9_idct_idct_8x8_add_12, 4, 6 + ARCH_X86_64, 14, \
                                  16 * mmsize + 3 * ARCH_X86_32 * mmsize, \
                                  dst, stride, block, eob
    mova                m0, [pw_4095]
    cmp               eobd, 1
    jg mangle(private_prefix %+ _ %+ vp9_idct_idct_8x8_add_10 %+ SUFFIX).idctfull

    ; dc-only - unfortunately, this one can overflow, since coefs are 18+sign
    ; bpp, and 18+14+sign does not fit in 32bit, so we do 2-stage multiplies
    DEFINE_ARGS dst, stride, block, coef, coefl
    pxor                m2, m2
    DC_ONLY_64BIT        5, m2
    movd                m1, coefd
    pshuflw             m1, m1, q0000
    punpcklqdq          m1, m1
    DEFINE_ARGS dst, stride, cnt
    mov               cntd, 4
.loop_dc:
    STORE_2x8            3, 4, 1, m2, m0
    lea               dstq, [dstq+strideq*2]
    dec               cntd
    jg .loop_dc
    RET

; inputs and outputs are dwords, coefficients are words
;
; dst1[hi]:dst3[lo] = src1 * coef1 + src2 * coef2
; dst2[hi]:dst4[lo] = src1 * coef2 - src2 * coef1
%macro SUMSUB_MUL_D 6-7 [pd_3fff] ; src/dst 1-2, dst3-4, coef1-2, mask
    pand               m%3, m%1, %7
    pand               m%4, m%2, %7
    psrad              m%1, 14
    psrad              m%2, 14
    packssdw           m%4, m%2
    packssdw           m%3, m%1
    punpckhwd          m%2, m%4, m%3
    punpcklwd          m%4, m%3
    pmaddwd            m%3, m%4, [pw_%6_%5]
    pmaddwd            m%1, m%2, [pw_%6_%5]
    pmaddwd            m%4, [pw_m%5_%6]
    pmaddwd            m%2, [pw_m%5_%6]
%endmacro

; dst1 = src2[hi]:src4[lo] + src1[hi]:src3[lo] + rnd >> 14
; dst2 = src2[hi]:src4[lo] - src1[hi]:src3[lo] + rnd >> 14
%macro SUMSUB_PACK_D 5-6 [pd_8192] ; src/dst 1-2, src3-4, tmp, rnd
    SUMSUB_BA        d, %1, %2, %5
    SUMSUB_BA        d, %3, %4, %5
    paddd              m%3, %6
    paddd              m%4, %6
    psrad              m%3, 14
    psrad              m%4, 14
    paddd              m%1, m%3
    paddd              m%2, m%4
%endmacro

%macro NEGD 1
%if cpuflag(ssse3)
    psignd              %1, [pw_m1]
%else
    pxor                %1, [pw_m1]
    paddd               %1, [pd_1]
%endif
%endmacro

; the following line has not been executed at the end of this macro:
; UNSCRATCH            6, 8, rsp+17*mmsize
%macro IADST8_1D 1-3 [pd_8192], [pd_3fff] ; src, rnd, mask
    mova                m0, [%1+ 0*mmsize]
    mova                m3, [%1+ 6*mmsize]
    mova                m4, [%1+ 8*mmsize]
    mova                m7, [%1+14*mmsize]
    SUMSUB_MUL_D         7, 0, 1, 2, 16305,  1606, %3   ; m7/1=t0a, m0/2=t1a
    SUMSUB_MUL_D         3, 4, 5, 6, 10394, 12665, %3   ; m3/5=t4a, m4/6=t5a
    SCRATCH              0, 8, rsp+17*mmsize
    SUMSUB_PACK_D        3, 7, 5, 1, 0, %2              ; m3=t0, m7=t4
    UNSCRATCH            0, 8, rsp+17*mmsize
    SUMSUB_PACK_D        4, 0, 6, 2, 1, %2              ; m4=t1, m0=t5

    SCRATCH              3, 8, rsp+17*mmsize
    SCRATCH              4, 9, rsp+18*mmsize
    SCRATCH              7, 10, rsp+19*mmsize
    SCRATCH              0, 11, rsp+20*mmsize

    mova                m1, [%1+ 2*mmsize]
    mova                m2, [%1+ 4*mmsize]
    mova                m5, [%1+10*mmsize]
    mova                m6, [%1+12*mmsize]
    SUMSUB_MUL_D         5, 2, 3, 4, 14449,  7723, %3   ; m5/8=t2a, m2/9=t3a
    SUMSUB_MUL_D         1, 6, 7, 0,  4756, 15679, %3   ; m1/10=t6a, m6/11=t7a
    SCRATCH              2, 12, rsp+21*mmsize
    SUMSUB_PACK_D        1, 5, 7, 3, 2, %2              ; m1=t2, m5=t6
    UNSCRATCH            2, 12, rsp+21*mmsize
    SUMSUB_PACK_D        6, 2, 0, 4, 3, %2              ; m6=t3, m2=t7

    UNSCRATCH            7, 10, rsp+19*mmsize
    UNSCRATCH            0, 11, rsp+20*mmsize
    SCRATCH              1, 10, rsp+19*mmsize
    SCRATCH              6, 11, rsp+20*mmsize

    SUMSUB_MUL_D         7, 0, 3, 4, 15137,  6270, %3   ; m7/8=t4a, m0/9=t5a
    SUMSUB_MUL_D         2, 5, 1, 6,  6270, 15137, %3   ; m2/10=t7a, m5/11=t6a
    SCRATCH              2, 12, rsp+21*mmsize
    SUMSUB_PACK_D        5, 7, 6, 3, 2, %2              ; m5=-out1, m7=t6
    UNSCRATCH            2, 12, rsp+21*mmsize
    NEGD                m5                              ; m5=out1
    SUMSUB_PACK_D        2, 0, 1, 4, 3, %2              ; m2=out6, m0=t7
    SUMSUB_MUL           7, 0, 3, 4, 11585, 11585, %2, %3   ; m7=out2, m0=-out5
    NEGD                m0                              ; m0=out5

    UNSCRATCH            3, 8, rsp+17*mmsize
    UNSCRATCH            4, 9, rsp+18*mmsize
    UNSCRATCH            1, 10, rsp+19*mmsize
    UNSCRATCH            6, 11, rsp+20*mmsize
    SCRATCH              2, 8, rsp+17*mmsize
    SCRATCH              0, 9, rsp+18*mmsize

    SUMSUB_BA         d, 1, 3,  2                       ; m1=out0, m3=t2
    SUMSUB_BA         d, 6, 4,  2                       ; m6=-out7, m4=t3
    NEGD                m6                              ; m6=out7
    SUMSUB_MUL           3, 4,  2,  0, 11585, 11585, %2, %3 ; m3=-out3, m4=out4
    NEGD                m3                              ; m3=out3

    UNSCRATCH            0, 9, rsp+18*mmsize

    SWAP                 0, 1, 5
    SWAP                 2, 7, 6
%endmacro

%macro IADST8_FN 5
cglobal vp9_%1_%3_8x8_add_10, 4, 6 + ARCH_X86_64, 16, \
                              16 * mmsize + ARCH_X86_32 * 6 * mmsize, \
                              dst, stride, block, eob
    mova                m0, [pw_1023]

.body:
    SCRATCH              0, 13, rsp+16*mmsize, max
    DEFINE_ARGS dst, stride, block, cnt, ptr, skip, dstbak
%if ARCH_X86_64
    mov            dstbakq, dstq
    movsxd            cntq, cntd
%endif
%if PIC
    lea               ptrq, [%5_8x8]
    movzx             cntd, byte [ptrq+cntq-1]
%else
    movzx             cntd, byte [%5_8x8+cntq-1]
%endif
    mov              skipd, 2
    sub              skipd, cntd
    mov               ptrq, rsp
    PRELOAD             14, pd_8192, rnd
    PRELOAD             15, pd_3fff, mask
.loop_1:
    %2_1D           blockq, reg_rnd, reg_mask

    TRANSPOSE4x4D        0, 1, 2, 3, 6
    mova  [ptrq+ 0*mmsize], m0
    mova  [ptrq+ 2*mmsize], m1
    mova  [ptrq+ 4*mmsize], m2
    mova  [ptrq+ 6*mmsize], m3
    UNSCRATCH            6, 8, rsp+17*mmsize
    TRANSPOSE4x4D        4, 5, 6, 7, 0
    mova  [ptrq+ 1*mmsize], m4
    mova  [ptrq+ 3*mmsize], m5
    mova  [ptrq+ 5*mmsize], m6
    mova  [ptrq+ 7*mmsize], m7
    add               ptrq, 8 * mmsize
    add             blockq, mmsize
    dec               cntd
    jg .loop_1

    ; zero-pad the remainder (skipped cols)
    test             skipd, skipd
    jz .end
    add              skipd, skipd
    lea             blockq, [blockq+skipq*(mmsize/2)]
    pxor                m0, m0
.loop_z:
    mova   [ptrq+mmsize*0], m0
    mova   [ptrq+mmsize*1], m0
    mova   [ptrq+mmsize*2], m0
    mova   [ptrq+mmsize*3], m0
    add               ptrq, 4 * mmsize
    dec              skipd
    jg .loop_z
.end:

    DEFINE_ARGS dst, stride, block, cnt, ptr, stride3, dstbak
    lea           stride3q, [strideq*3]
    mov               cntd, 2
    mov               ptrq, rsp
.loop_2:
    %4_1D             ptrq, reg_rnd, reg_mask

    pxor                m6, m6
    PRELOAD              9, pd_16, srnd
    ROUND_AND_STORE_4x4  0, 1, 2, 3, m6, reg_max, reg_srnd, 5
    lea               dstq, [dstq+strideq*4]
    UNSCRATCH            0, 8, rsp+17*mmsize
    UNSCRATCH            1, 13, rsp+16*mmsize, max
    UNSCRATCH            2, 9, pd_16, srnd
    ROUND_AND_STORE_4x4  4, 5, 0, 7, m6, m1, m2, 5
    add               ptrq, 16
%if ARCH_X86_64
    lea               dstq, [dstbakq+8]
%else
    mov               dstq, dstm
    add               dstq, 8
%endif
    dec               cntd
    jg .loop_2

    ; m6 is still zero
    ZERO_BLOCK blockq-2*mmsize, 32, 8, m6
    RET

cglobal vp9_%1_%3_8x8_add_12, 4, 6 + ARCH_X86_64, 16, \
                              16 * mmsize + ARCH_X86_32 * 6 * mmsize, \
                              dst, stride, block, eob
    mova                m0, [pw_4095]
    jmp mangle(private_prefix %+ _ %+ vp9_%1_%3_8x8_add_10 %+ SUFFIX).body
%endmacro

INIT_XMM sse2
IADST8_FN idct,  IDCT8,  iadst, IADST8, row
IADST8_FN iadst, IADST8, idct,  IDCT8,  col
IADST8_FN iadst, IADST8, iadst, IADST8, default

%macro IDCT16_1D 1-4 4 * mmsize, 65, 67 ; src, src_stride, stack_offset, mm32bit_stack_offset
    IDCT8_1D            %1, [pd_8192], [pd_3fff], %2 * 2, %4    ; m0-3=t0-3a, m4-5/m8|r67/m7=t4-7
    ; SCRATCH            6, 8, rsp+(%4+0)*mmsize    ; t6
    SCRATCH              0, 15, rsp+(%4+7)*mmsize   ; t0a
    SCRATCH              1, 14, rsp+(%4+6)*mmsize   ; t1a
    SCRATCH              2, 13, rsp+(%4+5)*mmsize   ; t2a
    SCRATCH              3, 12, rsp+(%4+4)*mmsize   ; t3a
    SCRATCH              4, 11, rsp+(%4+3)*mmsize   ; t4
    mova [rsp+(%3+0)*mmsize], m5                    ; t5
    mova [rsp+(%3+1)*mmsize], m7                    ; t7

    mova                m0, [%1+ 1*%2]              ; in1
    mova                m3, [%1+ 7*%2]              ; in7
    mova                m4, [%1+ 9*%2]              ; in9
    mova                m7, [%1+15*%2]              ; in15

    SUMSUB_MUL           0, 7, 1, 2, 16305,  1606   ; m0=t15a, m7=t8a
    SUMSUB_MUL           4, 3, 1, 2, 10394, 12665   ; m4=t14a, m3=t9a
    SUMSUB_BA         d, 3, 7, 1                    ; m3=t8, m7=t9
    SUMSUB_BA         d, 4, 0, 1                    ; m4=t15,m0=t14
    SUMSUB_MUL           0, 7, 1, 2, 15137,  6270   ; m0=t14a, m7=t9a

    mova                m1, [%1+ 3*%2]              ; in3
    mova                m2, [%1+ 5*%2]              ; in5
    mova                m5, [%1+11*%2]              ; in11
    mova                m6, [%1+13*%2]              ; in13

    SCRATCH              0,  9, rsp+(%4+1)*mmsize
    SCRATCH              7, 10, rsp+(%4+2)*mmsize

    SUMSUB_MUL           2, 5, 0, 7, 14449,  7723   ; m2=t13a, m5=t10a
    SUMSUB_MUL           6, 1, 0, 7,  4756, 15679   ; m6=t12a, m1=t11a
    SUMSUB_BA         d, 5, 1, 0                    ; m5=t11,m1=t10
    SUMSUB_BA         d, 2, 6, 0                    ; m2=t12,m6=t13
    NEGD                m1                          ; m1=-t10
    SUMSUB_MUL           1, 6, 0, 7, 15137,  6270   ; m1=t13a, m6=t10a

    UNSCRATCH            7, 10, rsp+(%4+2)*mmsize
    SUMSUB_BA         d, 5, 3, 0                    ; m5=t8a, m3=t11a
    SUMSUB_BA         d, 6, 7, 0                    ; m6=t9,  m7=t10
    SUMSUB_BA         d, 2, 4, 0                    ; m2=t15a,m4=t12a
    SCRATCH              5, 10, rsp+(%4+2)*mmsize
    SUMSUB_MUL           4, 3, 0, 5, 11585, 11585   ; m4=t12, m3=t11
    UNSCRATCH            0, 9, rsp+(%4+1)*mmsize
    SUMSUB_BA         d, 1, 0, 5                    ; m1=t14, m0=t13
    SCRATCH              6, 9, rsp+(%4+1)*mmsize
    SUMSUB_MUL           0, 7, 6, 5, 11585, 11585   ; m0=t13a,m7=t10a

    ; order: 15|r74,14|r73,13|r72,12|r71,11|r70,r65,8|r67,r66,10|r69,9|r68,7,3,4,0,1,2
    ; free: 6,5

    UNSCRATCH            5, 15, rsp+(%4+7)*mmsize
    SUMSUB_BA         d, 2, 5, 6                    ; m2=out0, m5=out15
    SCRATCH              5, 15, rsp+(%4+7)*mmsize
    UNSCRATCH            5, 14, rsp+(%4+6)*mmsize
    SUMSUB_BA         d, 1, 5, 6                    ; m1=out1, m5=out14
    SCRATCH              5, 14, rsp+(%4+6)*mmsize
    UNSCRATCH            5, 13, rsp+(%4+5)*mmsize
    SUMSUB_BA         d, 0, 5, 6                    ; m0=out2, m5=out13
    SCRATCH              5, 13, rsp+(%4+5)*mmsize
    UNSCRATCH            5, 12, rsp+(%4+4)*mmsize
    SUMSUB_BA         d, 4, 5, 6                    ; m4=out3, m5=out12
    SCRATCH              5, 12, rsp+(%4+4)*mmsize
    UNSCRATCH            5, 11, rsp+(%4+3)*mmsize
    SUMSUB_BA         d, 3, 5, 6                    ; m3=out4, m5=out11
    SCRATCH              4, 11, rsp+(%4+3)*mmsize
    mova                m4, [rsp+(%3+0)*mmsize]
    SUMSUB_BA         d, 7, 4, 6                    ; m7=out5, m4=out10
    mova [rsp+(%3+0)*mmsize], m5
    UNSCRATCH            5, 8, rsp+(%4+0)*mmsize
    UNSCRATCH            6, 9, rsp+(%4+1)*mmsize
    SCRATCH              2, 8, rsp+(%4+0)*mmsize
    SCRATCH              1, 9, rsp+(%4+1)*mmsize
    UNSCRATCH            1, 10, rsp+(%4+2)*mmsize
    SCRATCH              0, 10, rsp+(%4+2)*mmsize
    mova                m0, [rsp+(%3+1)*mmsize]
    SUMSUB_BA         d, 6, 5, 2                    ; m6=out6, m5=out9
    SUMSUB_BA         d, 1, 0, 2                    ; m1=out7, m0=out8

    SWAP                 0, 3, 1, 7, 2, 6, 4

    ; output order: 8-11|r67-70=out0-3
    ;               0-6,r65=out4-11
    ;               12-15|r71-74=out12-15
%endmacro

INIT_XMM sse2
cglobal vp9_idct_idct_16x16_add_10, 4, 6 + ARCH_X86_64, 16, \
                                    67 * mmsize + ARCH_X86_32 * 8 * mmsize, \
                                    dst, stride, block, eob
    mova                m0, [pw_1023]
    cmp               eobd, 1
    jg .idctfull

    ; dc-only - the 10bit version can be done entirely in 32bit, since the max
    ; coef values are 17+sign bit, and the coef is 14bit, so 31+sign easily
    ; fits in 32bit
    DEFINE_ARGS dst, stride, block, coef
    pxor                m2, m2
    DC_ONLY              6, m2
    movd                m1, coefd
    pshuflw             m1, m1, q0000
    punpcklqdq          m1, m1
    DEFINE_ARGS dst, stride, cnt
    mov               cntd, 8
.loop_dc:
    STORE_2x8            3, 4, 1, m2, m0, dstq,         mmsize
    STORE_2x8            3, 4, 1, m2, m0, dstq+strideq, mmsize
    lea               dstq, [dstq+strideq*2]
    dec               cntd
    jg .loop_dc
    RET

.idctfull:
    mova   [rsp+64*mmsize], m0
    DEFINE_ARGS dst, stride, block, cnt, ptr, skip, dstbak
%if ARCH_X86_64
    mov            dstbakq, dstq
    movsxd            cntq, cntd
%endif
%if PIC
    lea               ptrq, [default_16x16]
    movzx             cntd, byte [ptrq+cntq-1]
%else
    movzx             cntd, byte [default_16x16+cntq-1]
%endif
    mov              skipd, 4
    sub              skipd, cntd
    mov               ptrq, rsp
.loop_1:
    IDCT16_1D       blockq

    TRANSPOSE4x4D        0, 1, 2, 3, 7
    mova  [ptrq+ 1*mmsize], m0
    mova  [ptrq+ 5*mmsize], m1
    mova  [ptrq+ 9*mmsize], m2
    mova  [ptrq+13*mmsize], m3
    mova                m7, [rsp+65*mmsize]
    TRANSPOSE4x4D        4, 5, 6, 7, 0
    mova  [ptrq+ 2*mmsize], m4
    mova  [ptrq+ 6*mmsize], m5
    mova  [ptrq+10*mmsize], m6
    mova  [ptrq+14*mmsize], m7
    UNSCRATCH               0, 8, rsp+67*mmsize
    UNSCRATCH               1, 9, rsp+68*mmsize
    UNSCRATCH               2, 10, rsp+69*mmsize
    UNSCRATCH               3, 11, rsp+70*mmsize
    TRANSPOSE4x4D        0, 1, 2, 3, 7
    mova  [ptrq+ 0*mmsize], m0
    mova  [ptrq+ 4*mmsize], m1
    mova  [ptrq+ 8*mmsize], m2
    mova  [ptrq+12*mmsize], m3
    UNSCRATCH               4, 12, rsp+71*mmsize
    UNSCRATCH               5, 13, rsp+72*mmsize
    UNSCRATCH               6, 14, rsp+73*mmsize
    UNSCRATCH               7, 15, rsp+74*mmsize
    TRANSPOSE4x4D        4, 5, 6, 7, 0
    mova  [ptrq+ 3*mmsize], m4
    mova  [ptrq+ 7*mmsize], m5
    mova  [ptrq+11*mmsize], m6
    mova  [ptrq+15*mmsize], m7
    add               ptrq, 16 * mmsize
    add             blockq, mmsize
    dec               cntd
    jg .loop_1

    ; zero-pad the remainder (skipped cols)
    test             skipd, skipd
    jz .end
    add              skipd, skipd
    lea             blockq, [blockq+skipq*(mmsize/2)]
    pxor                m0, m0
.loop_z:
    mova   [ptrq+mmsize*0], m0
    mova   [ptrq+mmsize*1], m0
    mova   [ptrq+mmsize*2], m0
    mova   [ptrq+mmsize*3], m0
    mova   [ptrq+mmsize*4], m0
    mova   [ptrq+mmsize*5], m0
    mova   [ptrq+mmsize*6], m0
    mova   [ptrq+mmsize*7], m0
    add               ptrq, 8 * mmsize
    dec              skipd
    jg .loop_z
.end:

    DEFINE_ARGS dst, stride, block, cnt, ptr, stride3, dstbak
    lea           stride3q, [strideq*3]
    mov               cntd, 4
    mov               ptrq, rsp
.loop_2:
    IDCT16_1D         ptrq

    pxor               m7, m7
    lea               dstq, [dstq+strideq*4]
    ROUND_AND_STORE_4x4  0, 1, 2, 3, m7, [rsp+64*mmsize], [pd_32], 6
    lea               dstq, [dstq+strideq*4]
    mova                m0, [rsp+65*mmsize]
    mova                m1, [rsp+64*mmsize]
    mova                m2, [pd_32]
    ROUND_AND_STORE_4x4  4, 5, 6, 0, m7, m1, m2, 6

%if ARCH_X86_64
    DEFINE_ARGS dstbak, stride, block, cnt, ptr, stride3, dst
%else
    mov               dstq, dstm
%endif
    UNSCRATCH               0, 8, rsp+67*mmsize
    UNSCRATCH               4, 9, rsp+68*mmsize
    UNSCRATCH               5, 10, rsp+69*mmsize
    UNSCRATCH               3, 11, rsp+70*mmsize
    ROUND_AND_STORE_4x4  0, 4, 5, 3, m7, m1, m2, 6
%if ARCH_X86_64
    DEFINE_ARGS dst, stride, block, cnt, ptr, stride3, dstbak
    lea               dstq, [dstbakq+stride3q*4]
%else
    lea               dstq, [dstq+stride3q*4]
%endif
    UNSCRATCH               4, 12, rsp+71*mmsize
    UNSCRATCH               5, 13, rsp+72*mmsize
    UNSCRATCH               6, 14, rsp+73*mmsize
    UNSCRATCH               0, 15, rsp+74*mmsize
    ROUND_AND_STORE_4x4  4, 5, 6, 0, m7, m1, m2, 6

    add               ptrq, mmsize
%if ARCH_X86_64
    add            dstbakq, 8
    mov               dstq, dstbakq
%else
    add         dword dstm, 8
    mov               dstq, dstm
%endif
    dec               cntd
    jg .loop_2

    ; m7 is still zero
    ZERO_BLOCK blockq-4*mmsize, 64, 16, m7
    RET

INIT_XMM sse2
cglobal vp9_idct_idct_16x16_add_12, 4, 6 + ARCH_X86_64, 16, \
                                    67 * mmsize + ARCH_X86_32 * 8 * mmsize, \
                                    dst, stride, block, eob
    mova                m0, [pw_4095]
    cmp               eobd, 1
    jg mangle(private_prefix %+ _ %+ vp9_idct_idct_16x16_add_10 %+ SUFFIX).idctfull

    ; dc-only - unfortunately, this one can overflow, since coefs are 19+sign
    ; bpp, and 19+14+sign does not fit in 32bit, so we do 2-stage multiplies
    DEFINE_ARGS dst, stride, block, coef, coefl
    pxor                m2, m2
    DC_ONLY_64BIT        6, m2
    movd                m1, coefd
    pshuflw             m1, m1, q0000
    punpcklqdq          m1, m1
    DEFINE_ARGS dst, stride, cnt
    mov               cntd, 8
.loop_dc:
    STORE_2x8            3, 4, 1, m2, m0, dstq,         mmsize
    STORE_2x8            3, 4, 1, m2, m0, dstq+strideq, mmsize
    lea               dstq, [dstq+strideq*2]
    dec               cntd
    jg .loop_dc
    RET

; r65-69 are available for spills
; r70-77 are available on x86-32 only (x86-64 should use m8-15)
; output should be in m8-11|r70-73, m0-6,r65 and m12-15|r74-77
%macro IADST16_1D 1 ; src
    mova                m0, [%1+ 0*4*mmsize]        ; in0
    mova                m1, [%1+ 7*4*mmsize]        ; in7
    mova                m2, [%1+ 8*4*mmsize]        ; in8
    mova                m3, [%1+15*4*mmsize]        ; in15
    SUMSUB_MUL_D         3, 0, 4, 5, 16364,  804    ; m3/4=t0, m0/5=t1
    SUMSUB_MUL_D         1, 2, 6, 7, 11003, 12140   ; m1/6=t8, m2/7=t9
    SCRATCH              0, 8, rsp+70*mmsize
    SUMSUB_PACK_D        1, 3, 6, 4, 0              ; m1=t0a, m3=t8a
    UNSCRATCH            0, 8, rsp+70*mmsize
    SUMSUB_PACK_D        2, 0, 7, 5, 4              ; m2=t1a, m0=t9a
    mova   [rsp+67*mmsize], m1
    SCRATCH              2, 9, rsp+71*mmsize
    SCRATCH              3, 12, rsp+74*mmsize
    SCRATCH              0, 13, rsp+75*mmsize

    mova                m0, [%1+ 3*4*mmsize]        ; in3
    mova                m1, [%1+ 4*4*mmsize]        ; in4
    mova                m2, [%1+11*4*mmsize]        ; in11
    mova                m3, [%1+12*4*mmsize]        ; in12
    SUMSUB_MUL_D         2, 1, 4, 5, 14811,  7005   ; m2/4=t4, m1/5=t5
    SUMSUB_MUL_D         0, 3, 6, 7,  5520, 15426   ; m0/6=t12, m3/7=t13
    SCRATCH              1, 10, rsp+72*mmsize
    SUMSUB_PACK_D        0, 2, 6, 4, 1              ; m0=t4a, m2=t12a
    UNSCRATCH            1, 10, rsp+72*mmsize
    SUMSUB_PACK_D        3, 1, 7, 5, 4              ; m3=t5a, m1=t13a
    SCRATCH              0, 15, rsp+77*mmsize
    SCRATCH              3, 11, rsp+73*mmsize

    UNSCRATCH            0, 12, rsp+74*mmsize       ; t8a
    UNSCRATCH            3, 13, rsp+75*mmsize       ; t9a
    SUMSUB_MUL_D         0, 3, 4, 5, 16069,  3196   ; m0/4=t8, m3/5=t9
    SUMSUB_MUL_D         1, 2, 6, 7,  3196, 16069   ; m1/6=t13, m2/7=t12
    SCRATCH              1, 12, rsp+74*mmsize
    SUMSUB_PACK_D        2, 0, 7, 4, 1              ; m2=t8a, m0=t12a
    UNSCRATCH            1, 12, rsp+74*mmsize
    SUMSUB_PACK_D        1, 3, 6, 5, 4              ; m1=t9a, m3=t13a
    mova   [rsp+65*mmsize], m2
    mova   [rsp+66*mmsize], m1
    SCRATCH              0, 8, rsp+70*mmsize
    SCRATCH              3, 12, rsp+74*mmsize

    mova                m0, [%1+ 2*4*mmsize]        ; in2
    mova                m1, [%1+ 5*4*mmsize]        ; in5
    mova                m2, [%1+10*4*mmsize]        ; in10
    mova                m3, [%1+13*4*mmsize]        ; in13
    SUMSUB_MUL_D         3, 0, 4, 5, 15893,  3981   ; m3/4=t2, m0/5=t3
    SUMSUB_MUL_D         1, 2, 6, 7,  8423, 14053   ; m1/6=t10, m2/7=t11
    SCRATCH              0, 10, rsp+72*mmsize
    SUMSUB_PACK_D        1, 3, 6, 4, 0              ; m1=t2a, m3=t10a
    UNSCRATCH            0, 10, rsp+72*mmsize
    SUMSUB_PACK_D        2, 0, 7, 5, 4              ; m2=t3a, m0=t11a
    mova   [rsp+68*mmsize], m1
    mova   [rsp+69*mmsize], m2
    SCRATCH              3, 13, rsp+75*mmsize
    SCRATCH              0, 14, rsp+76*mmsize

    mova                m0, [%1+ 1*4*mmsize]        ; in1
    mova                m1, [%1+ 6*4*mmsize]        ; in6
    mova                m2, [%1+ 9*4*mmsize]        ; in9
    mova                m3, [%1+14*4*mmsize]        ; in14
    SUMSUB_MUL_D         2, 1, 4, 5, 13160,  9760   ; m2/4=t6, m1/5=t7
    SUMSUB_MUL_D         0, 3, 6, 7,  2404, 16207   ; m0/6=t14, m3/7=t15
    SCRATCH              1, 10, rsp+72*mmsize
    SUMSUB_PACK_D        0, 2, 6, 4, 1              ; m0=t6a, m2=t14a
    UNSCRATCH            1, 10, rsp+72*mmsize
    SUMSUB_PACK_D        3, 1, 7, 5, 4              ; m3=t7a, m1=t15a

    UNSCRATCH            4, 13, rsp+75*mmsize       ; t10a
    UNSCRATCH            5, 14, rsp+76*mmsize       ; t11a
    SCRATCH              0, 13, rsp+75*mmsize
    SCRATCH              3, 14, rsp+76*mmsize
    SUMSUB_MUL_D         4, 5, 6, 7,  9102, 13623   ; m4/6=t10, m5/7=t11
    SUMSUB_MUL_D         1, 2, 0, 3, 13623,  9102   ; m1/0=t15, m2/3=t14
    SCRATCH              0, 10, rsp+72*mmsize
    SUMSUB_PACK_D        2, 4, 3, 6, 0              ; m2=t10a, m4=t14a
    UNSCRATCH            0, 10, rsp+72*mmsize
    SUMSUB_PACK_D        1, 5, 0, 7, 6              ; m1=t11a, m5=t15a

    UNSCRATCH            0, 8, rsp+70*mmsize        ; t12a
    UNSCRATCH            3, 12, rsp+74*mmsize       ; t13a
    SCRATCH              2, 8, rsp+70*mmsize
    SCRATCH              1, 12, rsp+74*mmsize
    SUMSUB_MUL_D         0, 3, 1, 2, 15137,  6270   ; m0/1=t12, m3/2=t13
    SUMSUB_MUL_D         5, 4, 7, 6,  6270, 15137   ; m5/7=t15, m4/6=t14
    SCRATCH              2, 10, rsp+72*mmsize
    SUMSUB_PACK_D        4, 0, 6, 1, 2              ; m4=out2, m0=t14a
    UNSCRATCH            2, 10, rsp+72*mmsize
    SUMSUB_PACK_D        5, 3, 7, 2, 1              ; m5=-out13, m3=t15a
    NEGD                m5                          ; m5=out13

    UNSCRATCH            1, 9, rsp+71*mmsize        ; t1a
    mova                m2, [rsp+68*mmsize]         ; t2a
    UNSCRATCH            6, 13, rsp+75*mmsize       ; t6a
    UNSCRATCH            7, 14, rsp+76*mmsize       ; t7a
    SCRATCH              4, 10, rsp+72*mmsize
    SCRATCH              5, 13, rsp+75*mmsize
    UNSCRATCH            4, 15, rsp+77*mmsize       ; t4a
    UNSCRATCH            5, 11, rsp+73*mmsize       ; t5a
    SCRATCH              0, 14, rsp+76*mmsize
    SCRATCH              3, 15, rsp+77*mmsize
    mova                m0, [rsp+67*mmsize]         ; t0a
    SUMSUB_BA         d, 4, 0, 3                    ; m4=t0, m0=t4
    SUMSUB_BA         d, 5, 1, 3                    ; m5=t1, m1=t5
    SUMSUB_BA         d, 6, 2, 3                    ; m6=t2, m2=t6
    SCRATCH              4, 9, rsp+71*mmsize
    mova                m3, [rsp+69*mmsize]         ; t3a
    SUMSUB_BA         d, 7, 3, 4                    ; m7=t3, m3=t7

    mova   [rsp+67*mmsize], m5
    mova   [rsp+68*mmsize], m6
    mova   [rsp+69*mmsize], m7
    SUMSUB_MUL_D         0, 1, 4, 5, 15137,  6270   ; m0/4=t4a, m1/5=t5a
    SUMSUB_MUL_D         3, 2, 7, 6,  6270, 15137   ; m3/7=t7a, m2/6=t6a
    SCRATCH              1, 11, rsp+73*mmsize
    SUMSUB_PACK_D        2, 0, 6, 4, 1              ; m2=-out3, m0=t6
    NEGD                m2                          ; m2=out3
    UNSCRATCH            1, 11, rsp+73*mmsize
    SUMSUB_PACK_D        3, 1, 7, 5, 4              ; m3=out12, m1=t7
    SCRATCH              2, 11, rsp+73*mmsize
    UNSCRATCH            2, 12, rsp+74*mmsize       ; t11a
    SCRATCH              3, 12, rsp+74*mmsize

    UNSCRATCH            3, 8, rsp+70*mmsize        ; t10a
    mova                m4, [rsp+65*mmsize]         ; t8a
    mova                m5, [rsp+66*mmsize]         ; t9a
    SUMSUB_BA         d, 3, 4, 6                    ; m3=-out1, m4=t10
    NEGD                m3                          ; m3=out1
    SUMSUB_BA         d, 2, 5, 6                    ; m2=out14, m5=t11
    UNSCRATCH            6, 9, rsp+71*mmsize        ; t0
    UNSCRATCH            7, 14, rsp+76*mmsize       ; t14a
    SCRATCH              3, 9, rsp+71*mmsize
    SCRATCH              2, 14, rsp+76*mmsize

    SUMSUB_MUL           1, 0, 2, 3, 11585, 11585   ; m1=out4, m0=out11
    mova   [rsp+65*mmsize], m0
    SUMSUB_MUL           5, 4, 2, 3, 11585, 11585   ; m5=out6, m4=out9
    UNSCRATCH            0, 15, rsp+77*mmsize       ; t15a
    SUMSUB_MUL           7, 0, 2, 3, 11585, m11585  ; m7=out10, m0=out5

    mova                m2, [rsp+68*mmsize]         ; t2
    SUMSUB_BA         d, 2, 6, 3                    ; m2=out0, m6=t2a
    SCRATCH              2, 8, rsp+70*mmsize
    mova                m2, [rsp+67*mmsize]         ; t1
    mova                m3, [rsp+69*mmsize]         ; t3
    mova   [rsp+67*mmsize], m7
    SUMSUB_BA         d, 3, 2, 7                    ; m3=-out15, m2=t3a
    NEGD                m3                          ; m3=out15
    SCRATCH              3, 15, rsp+77*mmsize
    SUMSUB_MUL           6, 2, 7, 3, 11585, m11585  ; m6=out8, m2=out7
    mova                m7, [rsp+67*mmsize]

    SWAP                 0, 1
    SWAP                 2, 5, 4, 6, 7, 3
%endmacro

%macro IADST16_FN 7
cglobal vp9_%1_%4_16x16_add_10, 4, 6 + ARCH_X86_64, 16, \
                                70 * mmsize + ARCH_X86_32 * 8 * mmsize, \
                                dst, stride, block, eob
    mova                m0, [pw_1023]

.body:
    mova   [rsp+64*mmsize], m0
    DEFINE_ARGS dst, stride, block, cnt, ptr, skip, dstbak
%if ARCH_X86_64
    mov            dstbakq, dstq
    movsxd            cntq, cntd
%endif
%if PIC
    lea               ptrq, [%7_16x16]
    movzx             cntd, byte [ptrq+cntq-1]
%else
    movzx             cntd, byte [%7_16x16+cntq-1]
%endif
    mov              skipd, 4
    sub              skipd, cntd
    mov               ptrq, rsp
.loop_1:
    %2_1D           blockq

    TRANSPOSE4x4D        0, 1, 2, 3, 7
    mova  [ptrq+ 1*mmsize], m0
    mova  [ptrq+ 5*mmsize], m1
    mova  [ptrq+ 9*mmsize], m2
    mova  [ptrq+13*mmsize], m3
    mova                m7, [rsp+65*mmsize]
    TRANSPOSE4x4D        4, 5, 6, 7, 0
    mova  [ptrq+ 2*mmsize], m4
    mova  [ptrq+ 6*mmsize], m5
    mova  [ptrq+10*mmsize], m6
    mova  [ptrq+14*mmsize], m7
    UNSCRATCH               0, 8, rsp+(%3+0)*mmsize
    UNSCRATCH               1, 9, rsp+(%3+1)*mmsize
    UNSCRATCH               2, 10, rsp+(%3+2)*mmsize
    UNSCRATCH               3, 11, rsp+(%3+3)*mmsize
    TRANSPOSE4x4D        0, 1, 2, 3, 7
    mova  [ptrq+ 0*mmsize], m0
    mova  [ptrq+ 4*mmsize], m1
    mova  [ptrq+ 8*mmsize], m2
    mova  [ptrq+12*mmsize], m3
    UNSCRATCH               4, 12, rsp+(%3+4)*mmsize
    UNSCRATCH               5, 13, rsp+(%3+5)*mmsize
    UNSCRATCH               6, 14, rsp+(%3+6)*mmsize
    UNSCRATCH               7, 15, rsp+(%3+7)*mmsize
    TRANSPOSE4x4D        4, 5, 6, 7, 0
    mova  [ptrq+ 3*mmsize], m4
    mova  [ptrq+ 7*mmsize], m5
    mova  [ptrq+11*mmsize], m6
    mova  [ptrq+15*mmsize], m7
    add               ptrq, 16 * mmsize
    add             blockq, mmsize
    dec               cntd
    jg .loop_1

    ; zero-pad the remainder (skipped cols)
    test             skipd, skipd
    jz .end
    add              skipd, skipd
    lea             blockq, [blockq+skipq*(mmsize/2)]
    pxor                m0, m0
.loop_z:
    mova   [ptrq+mmsize*0], m0
    mova   [ptrq+mmsize*1], m0
    mova   [ptrq+mmsize*2], m0
    mova   [ptrq+mmsize*3], m0
    mova   [ptrq+mmsize*4], m0
    mova   [ptrq+mmsize*5], m0
    mova   [ptrq+mmsize*6], m0
    mova   [ptrq+mmsize*7], m0
    add               ptrq, 8 * mmsize
    dec              skipd
    jg .loop_z
.end:

    DEFINE_ARGS dst, stride, block, cnt, ptr, stride3, dstbak
    lea           stride3q, [strideq*3]
    mov               cntd, 4
    mov               ptrq, rsp
.loop_2:
    %5_1D             ptrq

    pxor                m7, m7
    lea               dstq, [dstq+strideq*4]
    ROUND_AND_STORE_4x4  0, 1, 2, 3, m7, [rsp+64*mmsize], [pd_32], 6
    lea               dstq, [dstq+strideq*4]
    mova                m0, [rsp+65*mmsize]
    mova                m1, [rsp+64*mmsize]
    mova                m2, [pd_32]
    ROUND_AND_STORE_4x4  4, 5, 6, 0, m7, m1, m2, 6

%if ARCH_X86_64
    DEFINE_ARGS dstbak, stride, block, cnt, ptr, stride3, dst
%else
    mov               dstq, dstm
%endif
    UNSCRATCH               0, 8, rsp+(%6+0)*mmsize
    UNSCRATCH               4, 9, rsp+(%6+1)*mmsize
    UNSCRATCH               5, 10, rsp+(%6+2)*mmsize
    UNSCRATCH               3, 11, rsp+(%6+3)*mmsize
    ROUND_AND_STORE_4x4  0, 4, 5, 3, m7, m1, m2, 6
%if ARCH_X86_64
    DEFINE_ARGS dst, stride, block, cnt, ptr, stride3, dstbak
    lea               dstq, [dstbakq+stride3q*4]
%else
    lea               dstq, [dstq+stride3q*4]
%endif
    UNSCRATCH               4, 12, rsp+(%6+4)*mmsize
    UNSCRATCH               5, 13, rsp+(%6+5)*mmsize
    UNSCRATCH               6, 14, rsp+(%6+6)*mmsize
    UNSCRATCH               0, 15, rsp+(%6+7)*mmsize
    ROUND_AND_STORE_4x4  4, 5, 6, 0, m7, m1, m2, 6

    add               ptrq, mmsize
%if ARCH_X86_64
    add            dstbakq, 8
    mov               dstq, dstbakq
%else
    add         dword dstm, 8
    mov               dstq, dstm
%endif
    dec               cntd
    jg .loop_2

    ; m7 is still zero
    ZERO_BLOCK blockq-4*mmsize, 64, 16, m7
    RET

cglobal vp9_%1_%4_16x16_add_12, 4, 6 + ARCH_X86_64, 16, \
                                70 * mmsize + ARCH_X86_32 * 8 * mmsize, \
                                dst, stride, block, eob
    mova                m0, [pw_4095]
    jmp mangle(private_prefix %+ _ %+ vp9_%1_%4_16x16_add_10 %+ SUFFIX).body
%endmacro

INIT_XMM sse2
IADST16_FN idct,  IDCT16,  67, iadst, IADST16, 70, row
IADST16_FN iadst, IADST16, 70, idct,  IDCT16,  67, col
IADST16_FN iadst, IADST16, 70, iadst, IADST16, 70, default

%macro IDCT32_1D 2-3 8 * mmsize; pass[1/2], src, src_stride
    IDCT16_1D %2, 2 * %3, 272, 257
%if ARCH_X86_64
    mova  [rsp+257*mmsize], m8
    mova  [rsp+258*mmsize], m9
    mova  [rsp+259*mmsize], m10
    mova  [rsp+260*mmsize], m11
    mova  [rsp+261*mmsize], m12
    mova  [rsp+262*mmsize], m13
    mova  [rsp+263*mmsize], m14
    mova  [rsp+264*mmsize], m15
%endif
    mova  [rsp+265*mmsize], m0
    mova  [rsp+266*mmsize], m1
    mova  [rsp+267*mmsize], m2
    mova  [rsp+268*mmsize], m3
    mova  [rsp+269*mmsize], m4
    mova  [rsp+270*mmsize], m5
    mova  [rsp+271*mmsize], m6

    ; r257-260: t0-3
    ; r265-272: t4/5a/6a/7/8/9a/10/11a
    ; r261-264: t12a/13/14a/15
    ; r273-274 is free as scratch space, and 275-282 mirrors m8-15 on 32bit

    mova                m0, [%2+ 1*%3]              ; in1
    mova                m1, [%2+15*%3]              ; in15
    mova                m2, [%2+17*%3]              ; in17
    mova                m3, [%2+31*%3]              ; in31
    SUMSUB_MUL           0, 3, 4, 5, 16364,  804    ; m0=t31a, m3=t16a
    SUMSUB_MUL           2, 1, 4, 5, 11003, 12140   ; m2=t30a, m1=t17a
    SUMSUB_BA         d, 1, 3, 4                    ; m1=t16, m3=t17
    SUMSUB_BA         d, 2, 0, 4                    ; m2=t31, m0=t30
    SUMSUB_MUL           0, 3, 4, 5, 16069,  3196   ; m0=t30a, m3=t17a
    SCRATCH              0, 8, rsp+275*mmsize
    SCRATCH              2, 9, rsp+276*mmsize

    ; end of stage 1-3 first quart

    mova                m0, [%2+ 7*%3]              ; in7
    mova                m2, [%2+ 9*%3]              ; in9
    mova                m4, [%2+23*%3]              ; in23
    mova                m5, [%2+25*%3]              ; in25
    SUMSUB_MUL           2, 4, 6, 7, 14811,  7005   ; m2=t29a, m4=t18a
    SUMSUB_MUL           5, 0, 6, 7,  5520, 15426   ; m5=t28a, m0=t19a
    SUMSUB_BA         d, 4, 0, 6                    ; m4=t19, m0=t18
    SUMSUB_BA         d, 2, 5, 6                    ; m2=t28, m5=t29
    SUMSUB_MUL           5, 0, 6, 7,  3196, m16069  ; m5=t29a, m0=t18a

    ; end of stage 1-3 second quart

    SUMSUB_BA         d, 4, 1, 6                    ; m4=t16a, m1=t19a
    SUMSUB_BA         d, 0, 3, 6                    ; m0=t17, m3=t18
    UNSCRATCH            6, 8, rsp+275*mmsize       ; t30a
    UNSCRATCH            7, 9, rsp+276*mmsize       ; t31
    mova  [rsp+273*mmsize], m4
    mova  [rsp+274*mmsize], m0
    SUMSUB_BA         d, 2, 7, 0                    ; m2=t31a, m7=t28a
    SUMSUB_BA         d, 5, 6, 0                    ; m5=t30, m6=t29
    SUMSUB_MUL           6, 3, 0, 4, 15137,  6270   ; m6=t29a, m3=t18a
    SUMSUB_MUL           7, 1, 0, 4, 15137,  6270   ; m7=t28, m1=t19
    SCRATCH              3, 10, rsp+277*mmsize
    SCRATCH              1, 11, rsp+278*mmsize
    SCRATCH              7, 12, rsp+279*mmsize
    SCRATCH              6, 13, rsp+280*mmsize
    SCRATCH              5, 14, rsp+281*mmsize
    SCRATCH              2, 15, rsp+282*mmsize

    ; end of stage 4-5 first half

    mova                m0, [%2+ 5*%3]              ; in5
    mova                m1, [%2+11*%3]              ; in11
    mova                m2, [%2+21*%3]              ; in21
    mova                m3, [%2+27*%3]              ; in27
    SUMSUB_MUL           0, 3, 4, 5, 15893,  3981   ; m0=t27a, m3=t20a
    SUMSUB_MUL           2, 1, 4, 5,  8423, 14053   ; m2=t26a, m1=t21a
    SUMSUB_BA         d, 1, 3, 4                    ; m1=t20, m3=t21
    SUMSUB_BA         d, 2, 0, 4                    ; m2=t27, m0=t26
    SUMSUB_MUL           0, 3, 4, 5,  9102, 13623   ; m0=t26a, m3=t21a
    SCRATCH              0, 8, rsp+275*mmsize
    SCRATCH              2, 9, rsp+276*mmsize

    ; end of stage 1-3 third quart

    mova                m0, [%2+ 3*%3]              ; in3
    mova                m2, [%2+13*%3]              ; in13
    mova                m4, [%2+19*%3]              ; in19
    mova                m5, [%2+29*%3]              ; in29
    SUMSUB_MUL           2, 4, 6, 7, 13160,  9760   ; m2=t25a, m4=t22a
    SUMSUB_MUL           5, 0, 6, 7,  2404, 16207   ; m5=t24a, m0=t23a
    SUMSUB_BA         d, 4, 0, 6                    ; m4=t23, m0=t22
    SUMSUB_BA         d, 2, 5, 6                    ; m2=t24, m5=t25
    SUMSUB_MUL           5, 0, 6, 7, 13623, m9102   ; m5=t25a, m0=t22a

    ; end of stage 1-3 fourth quart

    SUMSUB_BA         d, 1, 4, 6                    ; m1=t23a, m4=t20a
    SUMSUB_BA         d, 3, 0, 6                    ; m3=t22, m0=t21
    UNSCRATCH            6, 8, rsp+275*mmsize       ; t26a
    UNSCRATCH            7, 9, rsp+276*mmsize       ; t27
    SCRATCH              3, 8, rsp+275*mmsize
    SCRATCH              1, 9, rsp+276*mmsize
    SUMSUB_BA         d, 7, 2, 1                    ; m7=t24a, m2=t27a
    SUMSUB_BA         d, 6, 5, 1                    ; m6=t25, m5=t26
    SUMSUB_MUL           2, 4, 1, 3,  6270, m15137  ; m2=t27, m4=t20
    SUMSUB_MUL           5, 0, 1, 3,  6270, m15137  ; m5=t26a, m0=t21a

    ; end of stage 4-5 second half

    UNSCRATCH            1, 12, rsp+279*mmsize      ; t28
    UNSCRATCH            3, 13, rsp+280*mmsize      ; t29a
    SCRATCH              4, 12, rsp+279*mmsize
    SCRATCH              0, 13, rsp+280*mmsize
    SUMSUB_BA         d, 5, 3, 0                    ; m5=t29, m3=t26
    SUMSUB_BA         d, 2, 1, 0                    ; m2=t28a, m1=t27a
    UNSCRATCH            0, 14, rsp+281*mmsize      ; t30
    UNSCRATCH            4, 15, rsp+282*mmsize      ; t31a
    SCRATCH              2, 14, rsp+281*mmsize
    SCRATCH              5, 15, rsp+282*mmsize
    SUMSUB_BA         d, 6, 0, 2                    ; m6=t30a, m0=t25a
    SUMSUB_BA         d, 7, 4, 2                    ; m7=t31, m4=t24

    mova                m2, [rsp+273*mmsize]        ; t16a
    mova                m5, [rsp+274*mmsize]        ; t17
    mova  [rsp+273*mmsize], m6
    mova  [rsp+274*mmsize], m7
    UNSCRATCH            6, 10, rsp+277*mmsize      ; t18a
    UNSCRATCH            7, 11, rsp+278*mmsize      ; t19
    SCRATCH              4, 10, rsp+277*mmsize
    SCRATCH              0, 11, rsp+278*mmsize
    UNSCRATCH            4, 12, rsp+279*mmsize      ; t20
    UNSCRATCH            0, 13, rsp+280*mmsize      ; t21a
    SCRATCH              3, 12, rsp+279*mmsize
    SCRATCH              1, 13, rsp+280*mmsize
    SUMSUB_BA         d, 0, 6, 1                    ; m0=t18, m6=t21
    SUMSUB_BA         d, 4, 7, 1                    ; m4=t19a, m7=t20a
    UNSCRATCH            3, 8, rsp+275*mmsize       ; t22
    UNSCRATCH            1, 9, rsp+276*mmsize       ; t23a
    SCRATCH              0, 8, rsp+275*mmsize
    SCRATCH              4, 9, rsp+276*mmsize
    SUMSUB_BA         d, 3, 5, 0                    ; m3=t17a, m5=t22a
    SUMSUB_BA         d, 1, 2, 0                    ; m1=t16, m2=t23

    ; end of stage 6

    UNSCRATCH            0, 10, rsp+277*mmsize      ; t24
    UNSCRATCH            4, 11, rsp+278*mmsize      ; t25a
    SCRATCH              1, 10, rsp+277*mmsize
    SCRATCH              3, 11, rsp+278*mmsize
    SUMSUB_MUL           0, 2, 1, 3, 11585, 11585   ; m0=t24a, m2=t23a
    SUMSUB_MUL           4, 5, 1, 3, 11585, 11585   ; m4=t25, m5=t22
    UNSCRATCH            1, 12, rsp+279*mmsize      ; t26
    UNSCRATCH            3, 13, rsp+280*mmsize      ; t27a
    SCRATCH              0, 12, rsp+279*mmsize
    SCRATCH              4, 13, rsp+280*mmsize
    SUMSUB_MUL           3, 7, 0, 4, 11585, 11585   ; m3=t27, m7=t20
    SUMSUB_MUL           1, 6, 0, 4, 11585, 11585   ; m1=t26a, m6=t21a

    ; end of stage 7

    mova                m0, [rsp+269*mmsize]        ; t8
    mova                m4, [rsp+270*mmsize]        ; t9a
    mova  [rsp+269*mmsize], m1                      ; t26a
    mova  [rsp+270*mmsize], m3                      ; t27
    mova                m3, [rsp+271*mmsize]        ; t10
    SUMSUB_BA         d, 2, 0, 1                    ; m2=out8, m0=out23
    SUMSUB_BA         d, 5, 4, 1                    ; m5=out9, m4=out22
    SUMSUB_BA         d, 6, 3, 1                    ; m6=out10, m3=out21
    mova                m1, [rsp+272*mmsize]        ; t11a
    mova  [rsp+271*mmsize], m0
    SUMSUB_BA         d, 7, 1, 0                    ; m7=out11, m1=out20

%if %1 == 1
    TRANSPOSE4x4D        2, 5, 6, 7, 0
    mova  [ptrq+ 2*mmsize], m2
    mova  [ptrq+10*mmsize], m5
    mova  [ptrq+18*mmsize], m6
    mova  [ptrq+26*mmsize], m7
%else ; %1 == 2
    pxor                m0, m0
    lea               dstq, [dstq+strideq*8]
    ROUND_AND_STORE_4x4  2, 5, 6, 7, m0, [rsp+256*mmsize], [pd_32], 6
%endif
    mova                m2, [rsp+271*mmsize]
%if %1 == 1
    TRANSPOSE4x4D        1, 3, 4, 2, 0
    mova  [ptrq+ 5*mmsize], m1
    mova  [ptrq+13*mmsize], m3
    mova  [ptrq+21*mmsize], m4
    mova  [ptrq+29*mmsize], m2
%else ; %1 == 2
    lea               dstq, [dstq+stride3q*4]
    ROUND_AND_STORE_4x4  1, 3, 4, 2, m0, [rsp+256*mmsize], [pd_32], 6
%endif

    ; end of last stage + store for out8-11 and out20-23

    UNSCRATCH            0, 9, rsp+276*mmsize       ; t19a
    UNSCRATCH            1, 8, rsp+275*mmsize       ; t18
    UNSCRATCH            2, 11, rsp+278*mmsize      ; t17a
    UNSCRATCH            3, 10, rsp+277*mmsize      ; t16
    mova                m7, [rsp+261*mmsize]        ; t12a
    mova                m6, [rsp+262*mmsize]        ; t13
    mova                m5, [rsp+263*mmsize]        ; t14a
    SUMSUB_BA         d, 0, 7, 4                    ; m0=out12, m7=out19
    SUMSUB_BA         d, 1, 6, 4                    ; m1=out13, m6=out18
    SUMSUB_BA         d, 2, 5, 4                    ; m2=out14, m5=out17
    mova                m4, [rsp+264*mmsize]        ; t15
    SCRATCH              7, 8, rsp+275*mmsize
    SUMSUB_BA         d, 3, 4, 7                    ; m3=out15, m4=out16

%if %1 == 1
    TRANSPOSE4x4D        0, 1, 2, 3, 7
    mova  [ptrq+ 3*mmsize], m0
    mova  [ptrq+11*mmsize], m1
    mova  [ptrq+19*mmsize], m2
    mova  [ptrq+27*mmsize], m3
%else ; %1 == 2
%if ARCH_X86_64
    SWAP                 7, 9
    lea               dstq, [dstbakq+stride3q*4]
%else ; x86-32
    pxor                m7, m7
    mov               dstq, dstm
    lea               dstq, [dstq+stride3q*4]
%endif
    ROUND_AND_STORE_4x4  0, 1, 2, 3, m7, [rsp+256*mmsize], [pd_32], 6
%endif
    UNSCRATCH            0, 8, rsp+275*mmsize       ; out19
%if %1 == 1
    TRANSPOSE4x4D        4, 5, 6, 0, 7
    mova  [ptrq+ 4*mmsize], m4
    mova  [ptrq+12*mmsize], m5
    mova  [ptrq+20*mmsize], m6
    mova  [ptrq+28*mmsize], m0
%else ; %1 == 2
    lea               dstq, [dstq+strideq*4]
    ROUND_AND_STORE_4x4  4, 5, 6, 0, m7, [rsp+256*mmsize], [pd_32], 6
%endif

    ; end of last stage + store for out12-19

%if ARCH_X86_64
    SWAP                 7, 8
%endif
    mova                m7, [rsp+257*mmsize]        ; t0
    mova                m6, [rsp+258*mmsize]        ; t1
    mova                m5, [rsp+259*mmsize]        ; t2
    mova                m4, [rsp+260*mmsize]        ; t3
    mova                m0, [rsp+274*mmsize]        ; t31
    mova                m1, [rsp+273*mmsize]        ; t30a
    UNSCRATCH            2, 15, rsp+282*mmsize      ; t29
    SUMSUB_BA         d, 0, 7, 3                    ; m0=out0, m7=out31
    SUMSUB_BA         d, 1, 6, 3                    ; m1=out1, m6=out30
    SUMSUB_BA         d, 2, 5, 3                    ; m2=out2, m5=out29
    SCRATCH              0, 9, rsp+276*mmsize
    UNSCRATCH            3, 14, rsp+281*mmsize      ; t28a
    SUMSUB_BA         d, 3, 4, 0                    ; m3=out3, m4=out28

%if %1 == 1
    TRANSPOSE4x4D        4, 5, 6, 7, 0
    mova  [ptrq+ 7*mmsize], m4
    mova  [ptrq+15*mmsize], m5
    mova  [ptrq+23*mmsize], m6
    mova  [ptrq+31*mmsize], m7
%else ; %1 == 2
%if ARCH_X86_64
    SWAP                 0, 8
%else ; x86-32
    pxor                m0, m0
%endif
    lea               dstq, [dstq+stride3q*4]
    ROUND_AND_STORE_4x4  4, 5, 6, 7, m0, [rsp+256*mmsize], [pd_32], 6
%endif
    UNSCRATCH            7, 9, rsp+276*mmsize       ; out0
%if %1 == 1
    TRANSPOSE4x4D        7, 1, 2, 3, 0
    mova  [ptrq+ 0*mmsize], m7
    mova  [ptrq+ 8*mmsize], m1
    mova  [ptrq+16*mmsize], m2
    mova  [ptrq+24*mmsize], m3
%else ; %1 == 2
%if ARCH_X86_64
    DEFINE_ARGS dstbak, stride, block, cnt, ptr, stride3, dst
%else ; x86-32
    mov               dstq, dstm
%endif
    ROUND_AND_STORE_4x4  7, 1, 2, 3, m0, [rsp+256*mmsize], [pd_32], 6
%if ARCH_X86_64
    DEFINE_ARGS dst, stride, block, cnt, ptr, stride3, dstbak
%endif
%endif

    ; end of last stage + store for out0-3 and out28-31

%if ARCH_X86_64
    SWAP                 0, 8
%endif
    mova                m7, [rsp+265*mmsize]        ; t4
    mova                m6, [rsp+266*mmsize]        ; t5a
    mova                m5, [rsp+267*mmsize]        ; t6a
    mova                m4, [rsp+268*mmsize]        ; t7
    mova                m0, [rsp+270*mmsize]        ; t27
    mova                m1, [rsp+269*mmsize]        ; t26a
    UNSCRATCH            2, 13, rsp+280*mmsize      ; t25
    SUMSUB_BA         d, 0, 7, 3                    ; m0=out4, m7=out27
    SUMSUB_BA         d, 1, 6, 3                    ; m1=out5, m6=out26
    SUMSUB_BA         d, 2, 5, 3                    ; m2=out6, m5=out25
    UNSCRATCH            3, 12, rsp+279*mmsize      ; t24a
    SCRATCH              7, 9, rsp+276*mmsize
    SUMSUB_BA         d, 3, 4, 7                    ; m3=out7, m4=out24

%if %1 == 1
    TRANSPOSE4x4D        0, 1, 2, 3, 7
    mova  [ptrq+ 1*mmsize], m0
    mova  [ptrq+ 9*mmsize], m1
    mova  [ptrq+17*mmsize], m2
    mova  [ptrq+25*mmsize], m3
%else ; %1 == 2
%if ARCH_X86_64
    SWAP                 7, 8
    lea               dstq, [dstbakq+strideq*4]
%else ; x86-32
    pxor                m7, m7
    lea               dstq, [dstq+strideq*4]
%endif
    ROUND_AND_STORE_4x4  0, 1, 2, 3, m7, [rsp+256*mmsize], [pd_32], 6
%endif
    UNSCRATCH            0, 9, rsp+276*mmsize       ; out27
%if %1 == 1
    TRANSPOSE4x4D        4, 5, 6, 0, 7
    mova  [ptrq+ 6*mmsize], m4
    mova  [ptrq+14*mmsize], m5
    mova  [ptrq+22*mmsize], m6
    mova  [ptrq+30*mmsize], m0
%else ; %1 == 2
%if ARCH_X86_64
    lea               dstq, [dstbakq+stride3q*8]
%else
    mov               dstq, dstm
    lea               dstq, [dstq+stride3q*8]
%endif
    ROUND_AND_STORE_4x4  4, 5, 6, 0, m7, [rsp+256*mmsize], [pd_32], 6
%endif

    ; end of last stage + store for out4-7 and out24-27
%endmacro

INIT_XMM sse2
cglobal vp9_idct_idct_32x32_add_10, 4, 6 + ARCH_X86_64, 16, \
                                    275 * mmsize + ARCH_X86_32 * 8 * mmsize, \
                                    dst, stride, block, eob
    mova                m0, [pw_1023]
    cmp               eobd, 1
    jg .idctfull

    ; dc-only - the 10bit version can be done entirely in 32bit, since the max
    ; coef values are 17+sign bit, and the coef is 14bit, so 31+sign easily
    ; fits in 32bit
    DEFINE_ARGS dst, stride, block, coef
    pxor                m2, m2
    DC_ONLY              6, m2
    movd                m1, coefd
    pshuflw             m1, m1, q0000
    punpcklqdq          m1, m1
    DEFINE_ARGS dst, stride, cnt
    mov               cntd, 32
.loop_dc:
    STORE_2x8            3, 4, 1, m2, m0, dstq,          mmsize
    STORE_2x8            3, 4, 1, m2, m0, dstq+mmsize*2, mmsize
    add               dstq, strideq
    dec               cntd
    jg .loop_dc
    RET

.idctfull:
    mova  [rsp+256*mmsize], m0
    DEFINE_ARGS dst, stride, block, cnt, ptr, skip, dstbak
%if ARCH_X86_64
    mov            dstbakq, dstq
    movsxd            cntq, cntd
%endif
%if PIC
    lea               ptrq, [default_32x32]
    movzx             cntd, byte [ptrq+cntq-1]
%else
    movzx             cntd, byte [default_32x32+cntq-1]
%endif
    mov              skipd, 8
    sub              skipd, cntd
    mov               ptrq, rsp
.loop_1:
    IDCT32_1D            1, blockq

    add               ptrq, 32 * mmsize
    add             blockq, mmsize
    dec               cntd
    jg .loop_1

    ; zero-pad the remainder (skipped cols)
    test             skipd, skipd
    jz .end
    shl              skipd, 2
    lea             blockq, [blockq+skipq*(mmsize/4)]
    pxor                m0, m0
.loop_z:
    mova   [ptrq+mmsize*0], m0
    mova   [ptrq+mmsize*1], m0
    mova   [ptrq+mmsize*2], m0
    mova   [ptrq+mmsize*3], m0
    mova   [ptrq+mmsize*4], m0
    mova   [ptrq+mmsize*5], m0
    mova   [ptrq+mmsize*6], m0
    mova   [ptrq+mmsize*7], m0
    add               ptrq, 8 * mmsize
    dec              skipd
    jg .loop_z
.end:

    DEFINE_ARGS dst, stride, block, cnt, ptr, stride3, dstbak
    lea           stride3q, [strideq*3]
    mov               cntd, 8
    mov               ptrq, rsp
.loop_2:
    IDCT32_1D            2, ptrq

    add               ptrq, mmsize
%if ARCH_X86_64
    add            dstbakq, 8
    mov               dstq, dstbakq
%else
    add         dword dstm, 8
    mov               dstq, dstm
%endif
    dec               cntd
    jg .loop_2

    ; m7 is still zero
    ZERO_BLOCK blockq-8*mmsize, 128, 32, m7
    RET

INIT_XMM sse2
cglobal vp9_idct_idct_32x32_add_12, 4, 6 + ARCH_X86_64, 16, \
                                    275 * mmsize + ARCH_X86_32 * 8 * mmsize, \
                                    dst, stride, block, eob
    mova                m0, [pw_4095]
    cmp               eobd, 1
    jg mangle(private_prefix %+ _ %+ vp9_idct_idct_32x32_add_10 %+ SUFFIX).idctfull

    ; dc-only - unfortunately, this one can overflow, since coefs are 19+sign
    ; bpp, and 19+14+sign does not fit in 32bit, so we do 2-stage multiplies
    DEFINE_ARGS dst, stride, block, coef, coefl
    pxor                m2, m2
    DC_ONLY_64BIT        6, m2
    movd                m1, coefd
    pshuflw             m1, m1, q0000
    punpcklqdq          m1, m1
    DEFINE_ARGS dst, stride, cnt
    mov               cntd, 32
.loop_dc:
    STORE_2x8            3, 4, 1, m2, m0, dstq,          mmsize
    STORE_2x8            3, 4, 1, m2, m0, dstq+mmsize*2, mmsize
    add               dstq, strideq
    dec               cntd
    jg .loop_dc
    RET
