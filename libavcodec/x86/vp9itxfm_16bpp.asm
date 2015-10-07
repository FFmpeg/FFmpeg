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
cextern pd_8192

pd_8: times 4 dd 8
pd_3fff: times 4 dd 0x3fff

; FIXME these should probably be shared between 8bpp and 10/12bpp
pw_m11585_11585: times 4 dw -11585, 11585
pw_11585_11585: times 8 dw 11585
pw_m15137_6270: times 4 dw -15137, 6270
pw_6270_15137: times 4 dw 6270, 15137
pw_11585x2: times 8 dw 11585*2

pw_5283_13377: times 4 dw 5283, 13377
pw_9929_13377: times 4 dw 9929, 13377
pw_15212_m13377: times 4 dw 15212, -13377
pw_15212_9929: times 4 dw 15212, 9929
pw_m5283_m15212: times 4 dw -5283, -15212
pw_13377x2: times 8 dw 13377*2
pw_m13377_13377: times 4 dw -13377, 13377
pw_13377_0: times 4 dw 13377, 0
pw_9929_m5283: times 4 dw 9929, -5283

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

; 4x4 coefficients are 5+depth+sign bits, so for 10bpp, everything still fits
; in 15+1 words without additional effort, since the coefficients are 15bpp.

%macro IDCT4_10_FN 0
cglobal vp9_idct_idct_4x4_add_10, 4, 4, 8, dst, stride, block, eob
    cmp               eobd, 1
    jg .idctfull

    ; dc-only
%if cpuflag(ssse3)
    movd                m0, [blockq]
    mova                m5, [pw_11585x2]
    pmulhrsw            m0, m5
    pmulhrsw            m0, m5
%else
    DEFINE_ARGS dst, stride, block, coef
    mov              coefd, dword [blockq]
    imul             coefd, 11585
    add              coefd, 8192
    sar              coefd, 14
    imul             coefd, 11585
    add              coefd, (8 << 14) + 8192
    sar              coefd, 14 + 4
    movd                m0, coefd
%endif
    pshufw              m0, m0, 0
    pxor                m4, m4
    mova                m5, [pw_1023]
    movh          [blockq], m4
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
    WIN64_SPILL_XMM 8
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
%macro SUMSUB_MUL 6 ; src/dst 1-2, tmp1-2, coef1-2
    pand               m%3, m%1, [pd_3fff]
    pand               m%4, m%2, [pd_3fff]
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
    paddd              m%3, [pd_8192]
    paddd              m%4, [pd_8192]
    psrad              m%3, 14
    psrad              m%4, 14
    paddd              m%1, m%3
    paddd              m%2, m%4
%endmacro

%macro IDCT4_12BPP_1D 0
    SUMSUB_MUL           0, 2, 4, 5, 11585, 11585
    SUMSUB_MUL           1, 3, 4, 5, 15137,  6270
    SUMSUB_BA         d, 1, 0, 4
    SUMSUB_BA         d, 3, 2, 4
    SWAP                 1, 3, 0
%endmacro

INIT_XMM sse2
cglobal vp9_idct_idct_4x4_add_12, 4, 4, 6, dst, stride, block, eob
    cmp               eobd, 1
    jg .idctfull

    ; dc-only - this is special, since for 4x4 12bpp, the max coef size is
    ; 17+sign bpp. Since the multiply is with 11585, which is 14bpp, the
    ; result of each multiply is 31+sign bit, i.e. it _exactly_ fits in a
    ; dword. After the final shift (4), the result is 13+sign bits, so we
    ; don't need any additional processing to fit it in a word
    DEFINE_ARGS dst, stride, block, coef
    mov              coefd, dword [blockq]
    imul             coefd, 11585
    add              coefd, 8192
    sar              coefd, 14
    imul             coefd, 11585
    add              coefd, (8 << 14) + 8192
    sar              coefd, 14 + 4
    movd                m0, coefd
    pshuflw             m0, m0, q0000
    punpcklqdq          m0, m0
    pxor                m4, m4
    mova                m5, [pw_4095]
    movd          [blockq], m4
    DEFINE_ARGS dst, stride, stride3
    lea           stride3q, [strideq*3]
    movh                m1, [dstq+strideq*0]
    movh                m3, [dstq+strideq*2]
    movhps              m1, [dstq+strideq*1]
    movhps              m3, [dstq+stride3q ]
    paddw               m1, m0
    paddw               m3, m0
    pmaxsw              m1, m4
    pmaxsw              m3, m4
    pminsw              m1, m5
    pminsw              m3, m5
    movh   [dstq+strideq*0], m1
    movhps [dstq+strideq*1], m1
    movh   [dstq+strideq*2], m3
    movhps [dstq+stride3q ], m3
    RET

.idctfull:
    DEFINE_ARGS dst, stride, block, eob
    mova                m0, [blockq+0*16]
    mova                m1, [blockq+1*16]
    mova                m2, [blockq+2*16]
    mova                m3, [blockq+3*16]

    IDCT4_12BPP_1D
    TRANSPOSE4x4D        0, 1, 2, 3, 4
    IDCT4_12BPP_1D

    pxor                m4, m4
    ZERO_BLOCK      blockq, 16, 4, m4

    ; writeout
    DEFINE_ARGS dst, stride, stride3
    lea           stride3q, [strideq*3]
    paddd               m0, [pd_8]
    paddd               m1, [pd_8]
    paddd               m2, [pd_8]
    paddd               m3, [pd_8]
    psrad               m0, 4
    psrad               m1, 4
    psrad               m2, 4
    psrad               m3, 4
    packssdw            m0, m1
    packssdw            m2, m3
    mova                m5, [pw_4095]
    movh                m1, [dstq+strideq*0]
    movh                m3, [dstq+strideq*2]
    movhps              m1, [dstq+strideq*1]
    movhps              m3, [dstq+stride3q ]
    paddw               m0, m1
    paddw               m2, m3
    pmaxsw              m0, m4
    pmaxsw              m2, m4
    pminsw              m0, m5
    pminsw              m2, m5
    movh   [dstq+strideq*0], m0
    movhps [dstq+strideq*1], m0
    movh   [dstq+strideq*2], m2
    movhps [dstq+stride3q ], m2
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

; out0 =  5283 * in0 + 13377 + in1 + 15212 * in2 +  9929 * in3 + rnd >> 14
; out1 =  9929 * in0 + 13377 * in1 -  5283 * in2 - 15282 * in3 + rnd >> 14
; out2 = 13377 * in0               - 13377 * in2 + 13377 * in3 + rnd >> 14
; out3 = 15212 * in0 - 13377 * in1 +  9929 * in2 -  5283 * in3 + rnd >> 14
%macro IADST4_12BPP_1D 0
    pand                m4, m0, [pd_3fff]
    pand                m5, m1, [pd_3fff]
    psrad               m0, 14
    psrad               m1, 14
    packssdw            m5, m1
    packssdw            m4, m0
    punpckhwd           m1, m4, m5
    punpcklwd           m4, m5
    pand                m5, m2, [pd_3fff]
    pand                m6, m3, [pd_3fff]
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

    pmaddwd             m7, reg_b, [pw_15212_9929]
    pmaddwd             m6, m4, [pw_5283_13377]
    pmaddwd             m2, m3, [pw_15212_9929]
    pmaddwd             m0, reg_a, [pw_5283_13377]
    paddd               m6, m7
    paddd               m0, m2
    pmaddwd             m7, reg_b, [pw_m13377_13377]
    pmaddwd             m2, m4, [pw_13377_0]
    pmaddwd             m1, m3, [pw_m13377_13377]
    pmaddwd             m5, reg_a, [pw_13377_0]
    paddd               m2, m7
    paddd               m1, m5
    paddd               m6, [pd_8192]
    paddd               m2, [pd_8192]
    psrad               m6, 14
    psrad               m2, 14
    paddd               m0, m6                      ; t0
    paddd               m2, m1                      ; t2

    pmaddwd             m1, reg_b, [pw_m5283_m15212]
    pmaddwd             m6, m4, [pw_9929_13377]
    pmaddwd             m7, m3, [pw_m5283_m15212]
    pmaddwd             m5, reg_a, [pw_9929_13377]
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
    paddd               m6, [pd_8192]
    paddd               m4, [pd_8192]
    psrad               m6, 14
    psrad               m4, 14
    paddd               m7, m6                      ; t1
    paddd               m3, m4                      ; t3

    SWAP                 1, 7
%endmacro

%macro IADST4_12BPP_FN 4
cglobal vp9_%1_%3_4x4_add_12, 3, 3, 10, 2 * ARCH_X86_32 * mmsize, dst, stride, block, eob
    mova                m0, [blockq+0*16]
    mova                m1, [blockq+1*16]
    mova                m2, [blockq+2*16]
    mova                m3, [blockq+3*16]

    %2_12BPP_1D
    TRANSPOSE4x4D        0, 1, 2, 3, 4
    %4_12BPP_1D

    pxor                m4, m4
    ZERO_BLOCK      blockq, 16, 4, m4

    ; writeout
    DEFINE_ARGS dst, stride, stride3
    lea           stride3q, [strideq*3]
    paddd               m0, [pd_8]
    paddd               m1, [pd_8]
    paddd               m2, [pd_8]
    paddd               m3, [pd_8]
    psrad               m0, 4
    psrad               m1, 4
    psrad               m2, 4
    psrad               m3, 4
    packssdw            m0, m1
    packssdw            m2, m3
    mova                m5, [pw_4095]
    movh                m1, [dstq+strideq*0]
    movh                m3, [dstq+strideq*2]
    movhps              m1, [dstq+strideq*1]
    movhps              m3, [dstq+stride3q ]
    paddw               m0, m1
    paddw               m2, m3
    pmaxsw              m0, m4
    pmaxsw              m2, m4
    pminsw              m0, m5
    pminsw              m2, m5
    movh   [dstq+strideq*0], m0
    movhps [dstq+strideq*1], m0
    movh   [dstq+strideq*2], m2
    movhps [dstq+stride3q ], m2
    RET
%endmacro

INIT_XMM sse2
IADST4_12BPP_FN idct,  IDCT4,  iadst, IADST4
IADST4_12BPP_FN iadst, IADST4, idct,  IDCT4
IADST4_12BPP_FN iadst, IADST4, iadst, IADST4
