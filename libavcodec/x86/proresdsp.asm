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

%include "libavutil/x86/x86util.asm"

%if ARCH_X86_64

SECTION_RODATA

pw_88:      times 8 dw 0x2008
cextern pw_1
cextern pw_4
cextern pw_1019
; Below are defined in simple_idct10.asm built from selecting idctdsp
cextern w4_plus_w2_hi
cextern w4_min_w2_hi
cextern w4_plus_w6_hi
cextern w4_min_w6_hi
cextern w1_plus_w3_hi
cextern w3_min_w1_hi
cextern w7_plus_w3_hi
cextern w3_min_w7_hi
cextern w1_plus_w5
cextern w5_min_w1
cextern w5_plus_w7
cextern w7_min_w5

%include "libavcodec/x86/simple_idct10_template.asm"

%if HAVE_AVX2_EXTERNAL
align 32
pd_w1_12: times 8 dd 45451
align 32
pd_w2_12: times 8 dd 42813
align 32
pd_w3_12: times 8 dd 38531
align 32
pd_w4_12: times 8 dd 32767
align 32
pd_w5_12: times 8 dd 25746
align 32
pd_w6_12: times 8 dd 17734
align 32
pd_w7_12: times 8 dd  9041
align 32
pd_round_row12: times 8 dd 1<<(16-1)
pw_col_bias_12: times 8 dw 8192
pd_col_round12: times 8 dd 2
pw_4091:    times 8 dw 4091
%endif

SECTION .text

define_constants _hi

%macro idct_fn 0
cglobal prores_idct_put_10, 4, 4, 14, pixels, lsize, block, qmat
    IDCT_FN    pw_1, 15, pw_88, 18, "put", pw_4, pw_1019, r3
    RET
%endmacro

INIT_XMM sse2
idct_fn
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
idct_fn
%endif

%if HAVE_AVX2_EXTERNAL
%macro AB_TERMS_12_AVX2 1
    pmulld      m8, m0, [pd_w4_12]
%if %1
    paddd       m8, [pd_round_row12]
%endif
    pmulld      m9, m2, [pd_w2_12]
    paddd       m8, m9
    pmulld      m9, m4, [pd_w4_12]
    paddd       m8, m9
    pmulld      m9, m6, [pd_w6_12]
    paddd       m8, m9

    pmulld      m10, m0, [pd_w4_12]
%if %1
    paddd       m10, [pd_round_row12]
%endif
    pmulld      m9, m2, [pd_w6_12]
    paddd       m10, m9
    pmulld      m9, m4, [pd_w4_12]
    psubd       m10, m9
    pmulld      m9, m6, [pd_w2_12]
    psubd       m10, m9

    pmulld      m11, m0, [pd_w4_12]
%if %1
    paddd       m11, [pd_round_row12]
%endif
    pmulld      m9, m2, [pd_w6_12]
    psubd       m11, m9
    pmulld      m9, m4, [pd_w4_12]
    psubd       m11, m9
    pmulld      m9, m6, [pd_w2_12]
    paddd       m11, m9

    pmulld      m12, m0, [pd_w4_12]
%if %1
    paddd       m12, [pd_round_row12]
%endif
    pmulld      m9, m2, [pd_w2_12]
    psubd       m12, m9
    pmulld      m9, m4, [pd_w4_12]
    paddd       m12, m9
    pmulld      m9, m6, [pd_w6_12]
    psubd       m12, m9

    pmulld      m13, m1, [pd_w1_12]
    pmulld      m9,  m3, [pd_w3_12]
    paddd       m13, m9
    pmulld      m9,  m5, [pd_w5_12]
    paddd       m13, m9
    pmulld      m9,  m7, [pd_w7_12]
    paddd       m13, m9

    pmulld      m14, m1, [pd_w3_12]
    pmulld      m9,  m3, [pd_w7_12]
    psubd       m14, m9
    pmulld      m9,  m5, [pd_w1_12]
    psubd       m14, m9
    pmulld      m9,  m7, [pd_w5_12]
    psubd       m14, m9

    pmulld      m15, m1, [pd_w5_12]
    pmulld      m9,  m3, [pd_w1_12]
    psubd       m15, m9
    pmulld      m9,  m5, [pd_w7_12]
    paddd       m15, m9
    pmulld      m9,  m7, [pd_w3_12]
    paddd       m15, m9

    pmulld      m0, m1, [pd_w7_12]
    pmulld      m9, m3, [pd_w5_12]
    psubd       m0, m9
    pmulld      m9, m5, [pd_w3_12]
    paddd       m0, m9
    pmulld      m9, m7, [pd_w1_12]
    psubd       m0, m9
%endmacro

%macro COMBINE_PACK_ROW_REG_12 6
    paddd        m%4, %1, %2
    psrad        m%4, %3
    psubd        m%5, %1, %2
    psrad        m%5, %3
    vextracti128 xm%6, m%4, 1
    packssdw     xm%4, xm%6
    vextracti128 xm%6, m%5, 1
    packssdw     xm%5, xm%6
%endmacro

%macro COMBINE_PACK_CLIP_PUT_12 5
    paddd        m1, %1, %2
    psubd        m2, %1, %2
    psrad        m1, %3
    psrad        m2, %3
    vextracti128 xm3, m1, 0
    vextracti128 xm4, m1, 1
    packssdw     xm3, xm4
    pmaxsw       xm3, [pw_4]
    pminsw       xm3, [pw_4091]
    imul         r4, lsizeq, %4
    mova         [pixelsq+r4], xm3
    vextracti128 xm3, m2, 0
    vextracti128 xm4, m2, 1
    packssdw     xm3, xm4
    pmaxsw       xm3, [pw_4]
    pminsw       xm3, [pw_4091]
    imul         r4, lsizeq, %5
    mova         [pixelsq+r4], xm3
%endmacro

INIT_YMM avx2
cglobal prores_idct_put_12, 4, 5, 16, 32, pixels, lsize, block, qmat
    movu        xm0,[blockq+0*16]
    pmullw      xm0,[qmatq+0*16]
    movu        xm1,[blockq+1*16]
    pmullw      xm1,[qmatq+1*16]
    movu        xm2,[blockq+2*16]
    pmullw      xm2,[qmatq+2*16]
    movu        xm3,[blockq+3*16]
    pmullw      xm3,[qmatq+3*16]
    movu        xm4,[blockq+4*16]
    pmullw      xm4,[qmatq+4*16]
    movu        xm5,[blockq+5*16]
    pmullw      xm5,[qmatq+5*16]
    movu        xm6,[blockq+6*16]
    pmullw      xm6,[qmatq+6*16]
    movu        xm7,[blockq+7*16]
    pmullw      xm7,[qmatq+7*16]

    por         xm14, xm1, xm2
    por         xm14, xm14, xm3
    por         xm14, xm14, xm4
    por         xm14, xm14, xm5
    por         xm14, xm14, xm6
    por         xm14, xm14, xm7
    pxor        xm9, xm9
    pcmpeqw     xm14, xm9
    paddw       xm15, xm0, [pw_1]
    psraw       xm15, 1
    mova        [rsp+0], xm14
    mova        [rsp+16], xm15

    vpmovsxwd   m0, xm0
    vpmovsxwd   m1, xm1
    vpmovsxwd   m2, xm2
    vpmovsxwd   m3, xm3
    vpmovsxwd   m4, xm4
    vpmovsxwd   m5, xm5
    vpmovsxwd   m6, xm6
    vpmovsxwd   m7, xm7

    AB_TERMS_12_AVX2 1

    COMBINE_PACK_ROW_REG_12 m8, m13, 16, 9, 7, 3
    COMBINE_PACK_ROW_REG_12 m10,m14, 16, 1, 6, 3
    COMBINE_PACK_ROW_REG_12 m11,m15, 16, 2, 5, 3
    COMBINE_PACK_ROW_REG_12 m12,m0,  16, 3, 4, 0

    mova        xm8, [rsp+0]
    mova        xm10,[rsp+16]
    pand        xm10, xm8
    pandn       xm9, xm8, xm9
    por         xm9, xm10
    pandn       xm1, xm8, xm1
    por         xm1, xm10
    pandn       xm2, xm8, xm2
    por         xm2, xm10
    pandn       xm3, xm8, xm3
    por         xm3, xm10
    pandn       xm4, xm8, xm4
    por         xm4, xm10
    pandn       xm5, xm8, xm5
    por         xm5, xm10
    pandn       xm6, xm8, xm6
    por         xm6, xm10
    pandn       xm7, xm8, xm7
    por         xm7, xm10
    mova        xm0, xm9

    mova        [blockq+0*16], xm0
    mova        [blockq+1*16], xm1
    mova        [blockq+2*16], xm2
    mova        [blockq+3*16], xm3
    mova        [blockq+4*16], xm4
    mova        [blockq+5*16], xm5
    mova        [blockq+6*16], xm6
    mova        [blockq+7*16], xm7

    INIT_XMM avx
    mova        xm0,[blockq+0*16]
    mova        xm1,[blockq+1*16]
    mova        xm2,[blockq+2*16]
    mova        xm3,[blockq+3*16]
    mova        xm4,[blockq+4*16]
    mova        xm5,[blockq+5*16]
    mova        xm6,[blockq+6*16]
    mova        xm7,[blockq+7*16]
    TRANSPOSE8x8W 0,1,2,3,4,5,6,7,8
    mova        [blockq+0*16],xm0
    mova        [blockq+1*16],xm1
    mova        [blockq+2*16],xm2
    mova        [blockq+3*16],xm3
    mova        [blockq+4*16],xm4
    mova        [blockq+5*16],xm5
    mova        [blockq+6*16],xm6
    mova        [blockq+7*16],xm7
    INIT_YMM avx2

    movu        xm0,[blockq+0*16]
    paddw       xm0,[pw_col_bias_12]
    vpmovsxwd   m0, xm0
    paddd       m0, [pd_col_round12]
    movu        xm1,[blockq+1*16]
    vpmovsxwd   m1, xm1
    movu        xm2,[blockq+2*16]
    vpmovsxwd   m2, xm2
    movu        xm3,[blockq+3*16]
    vpmovsxwd   m3, xm3
    movu        xm4,[blockq+4*16]
    vpmovsxwd   m4, xm4
    movu        xm5,[blockq+5*16]
    vpmovsxwd   m5, xm5
    movu        xm6,[blockq+6*16]
    vpmovsxwd   m6, xm6
    movu        xm7,[blockq+7*16]
    vpmovsxwd   m7, xm7

    AB_TERMS_12_AVX2 0

    COMBINE_PACK_CLIP_PUT_12 m8, m13, 17, 0, 7
    COMBINE_PACK_CLIP_PUT_12 m10,m14, 17, 1, 6
    COMBINE_PACK_CLIP_PUT_12 m11,m15, 17, 2, 5
    COMBINE_PACK_CLIP_PUT_12 m12,m0,  17, 3, 4
    RET
%endif

%endif
