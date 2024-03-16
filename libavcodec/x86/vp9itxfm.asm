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
%include "vp9itxfm_template.asm"

SECTION_RODATA 32

%macro VP9_IDCT_COEFFS 2-3 0
const pw_m%1_%2
times 8 dw -%1,  %2
const pw_%2_%1
times 8 dw  %2,  %1

%if %3 == 1
const pw_m%2_m%1
times 8 dw -%2, -%1
%if %1 != %2
const pw_m%2_%1
times 8 dw -%2,  %1
const pw_%1_%2
times 8 dw  %1,  %2
%endif
%endif

%if %1 < 11585
pw_m%1x2:   times 16 dw -%1*2
%elif %1 > 11585
pw_%1x2:    times 16 dw  %1*2
%else
const pw_%1x2
times 16 dw %1*2
%endif

%if %2 != %1
pw_%2x2:    times 16 dw  %2*2
%endif
%endmacro

VP9_IDCT_COEFFS 16364,   804
VP9_IDCT_COEFFS 16305,  1606
VP9_IDCT_COEFFS 16069,  3196, 1
VP9_IDCT_COEFFS 15893,  3981
VP9_IDCT_COEFFS 15137,  6270, 1
VP9_IDCT_COEFFS 14811,  7005
VP9_IDCT_COEFFS 14449,  7723
VP9_IDCT_COEFFS 13160,  9760
VP9_IDCT_COEFFS 11585, 11585, 1
VP9_IDCT_COEFFS 11003, 12140
VP9_IDCT_COEFFS 10394, 12665
VP9_IDCT_COEFFS  9102, 13623, 1
VP9_IDCT_COEFFS  8423, 14053
VP9_IDCT_COEFFS  5520, 15426
VP9_IDCT_COEFFS  4756, 15679
VP9_IDCT_COEFFS  2404, 16207

const pw_5283_13377
times 4 dw 5283, 13377
const pw_9929_13377
times 4 dw 9929, 13377
const pw_15212_m13377
times 4 dw 15212, -13377
const pw_15212_9929
times 4 dw 15212, 9929
const pw_m5283_m15212
times 4 dw -5283, -15212
const pw_13377x2
times 8 dw 13377*2
const pw_m13377_13377
times 4 dw -13377, 13377
const pw_13377_0
times 4 dw 13377, 0

cextern pw_8
cextern pw_16
cextern pw_32
cextern pw_512
cextern pw_1024
cextern pw_2048
cextern pw_m1
cextern pd_8192

SECTION .text

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
%if mmsize == 32
    pmovzxbw           m%3, [%6]
    pmovzxbw           m%4, [%6+strideq]
%else
    movh               m%3, [%6]
    movh               m%4, [%6+strideq]
    punpcklbw          m%3, m%5
    punpcklbw          m%4, m%5
%endif
    paddw              m%3, m%1
    paddw              m%4, m%2
%if mmsize == 32
    packuswb           m%3, m%4
    ; Intel...
    vpermq             m%3, m%3, q3120
    mova              [%6], xm%3
    vextracti128 [%6+strideq], m%3, 1
%elif mmsize == 16
    packuswb           m%3, m%4
    movh              [%6], m%3
    movhps    [%6+strideq], m%3
%else
    packuswb           m%3, m%5
    packuswb           m%4, m%5
    movh              [%6], m%3
    movh      [%6+strideq], m%4
%endif
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
%if cpuflag(ssse3)
    mova                m5, [pw_2048]
    pmulhrsw            m0, m5              ; (x*2048 + (1<<14))>>15 <=> (x+8)>>4
    pmulhrsw            m1, m5
%else
    mova                m5, [pw_8]
    paddw               m0, m5
    paddw               m1, m5
    psraw               m0, 4
    psraw               m1, 4
%endif
    VP9_STORE_2X         0,  1,  6,  7,  4
    lea               dstq, [dstq+2*strideq]
%if cpuflag(ssse3)
    pmulhrsw            m2, m5
    pmulhrsw            m3, m5
%else
    paddw               m2, m5
    paddw               m3, m5
    psraw               m2, 4
    psraw               m3, 4
%endif
    VP9_STORE_2X         2,  3,  6,  7,  4
%endmacro

%macro IDCT_4x4_FN 1
INIT_MMX %1
cglobal vp9_idct_idct_4x4_add, 4, 4, 0, dst, stride, block, eob

%if cpuflag(ssse3)
    cmp eobd, 4 ; 2x2 or smaller
    jg .idctfull

    cmp eobd, 1 ; faster path for when only DC is set
    jne .idct2x2
%else
    cmp eobd, 1
    jg .idctfull
%endif

%if cpuflag(ssse3)
    movd                m0, [blockq]
    mova                m5, [pw_11585x2]
    pmulhrsw            m0, m5
    pmulhrsw            m0, m5
%else
    DEFINE_ARGS dst, stride, block, coef
    movsx            coefd, word [blockq]
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
    movh          [blockq], m4
%if cpuflag(ssse3)
    pmulhrsw            m0, [pw_2048]       ; (x*2048 + (1<<14))>>15 <=> (x+8)>>4
%endif
    VP9_STORE_2X         0,  0,  6,  7,  4
    lea               dstq, [dstq+2*strideq]
    VP9_STORE_2X         0,  0,  6,  7,  4
    RET

%if cpuflag(ssse3)
; faster path for when only top left 2x2 block is set
.idct2x2:
    movd                m0, [blockq+0]
    movd                m1, [blockq+8]
    mova                m5, [pw_11585x2]
    mova                m6, [pw_6270x2]
    mova                m7, [pw_15137x2]
    VP9_IDCT4_2x2_1D
    ; partial 2x4 transpose
    punpcklwd           m0, m1
    punpcklwd           m2, m3
    SBUTTERFLY          dq, 0, 2, 1
    SWAP                1, 2
    VP9_IDCT4_2x2_1D
    pxor                m4, m4  ; used for the block reset, and VP9_STORE_2X
    movh       [blockq+ 0], m4
    movh       [blockq+ 8], m4
    VP9_IDCT4_WRITEOUT
    RET
%endif

.idctfull: ; generic full 4x4 idct/idct
    mova                m0, [blockq+ 0]
    mova                m1, [blockq+ 8]
    mova                m2, [blockq+16]
    mova                m3, [blockq+24]
%if cpuflag(ssse3)
    mova                m6, [pw_11585x2]
%endif
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
%endmacro

IDCT_4x4_FN mmxext
IDCT_4x4_FN ssse3

;-------------------------------------------------------------------------------------------
; void vp9_iadst_iadst_4x4_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;-------------------------------------------------------------------------------------------

%macro IADST4_FN 5
INIT_MMX %5
cglobal vp9_%1_%3_4x4_add, 3, 3, 0, dst, stride, block, eob
%if WIN64 && notcpuflag(ssse3)
INIT_XMM cpuname
    WIN64_SPILL_XMM 8
INIT_MMX cpuname
%endif
    movdqa            xmm5, [pd_8192]
    mova                m0, [blockq+ 0]
    mova                m1, [blockq+ 8]
    mova                m2, [blockq+16]
    mova                m3, [blockq+24]
%if cpuflag(ssse3)
    mova                m6, [pw_11585x2]
%endif
%ifnidn %1%3, iadstiadst
    movdq2q             m7, xmm5
%endif
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

IADST4_FN idct,  IDCT4,  iadst, IADST4, sse2
IADST4_FN iadst, IADST4, idct,  IDCT4,  sse2
IADST4_FN iadst, IADST4, iadst, IADST4, sse2

IADST4_FN idct,  IDCT4,  iadst, IADST4, ssse3
IADST4_FN iadst, IADST4, idct,  IDCT4,  ssse3
IADST4_FN iadst, IADST4, iadst, IADST4, ssse3

%macro SCRATCH 3
%if ARCH_X86_64
    SWAP                %1, %2
%else
    mova              [%3], m%1
%endif
%endmacro

%macro UNSCRATCH 3
%if ARCH_X86_64
    SWAP                %1, %2
%else
    mova               m%1, [%3]
%endif
%endmacro

;-------------------------------------------------------------------------------------------
; void vp9_idct_idct_8x8_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;-------------------------------------------------------------------------------------------

%macro VP9_IDCT8_1D_FINALIZE 0
    SUMSUB_BA            w,  3,  6, 5                       ; m3=t0+t7, m6=t0-t7
    SUMSUB_BA            w,  1,  2, 5                       ; m1=t1+t6, m2=t1-t6
    SUMSUB_BA            w,  7,  0, 5                       ; m7=t2+t5, m0=t2-t5

    UNSCRATCH            5, 8, blockq+ 0
    SCRATCH              2, 8, blockq+ 0

    SUMSUB_BA            w,  5,  4, 2                       ; m5=t3+t4, m4=t3-t4
    SWAP                 7,  6,  2
    SWAP                 3,  5,  0

%if ARCH_X86_64
    SWAP                 6, 8
%endif
%endmacro

; x86-32
; - in: m0/m4 is in mem
; - out: m6 is in mem
; x86-64:
; - everything is in registers (m0-7)
%macro VP9_IDCT8_1D 0
%if ARCH_X86_64
    SWAP                 0, 8
    SWAP                 4, 9
%endif

    VP9_UNPACK_MULSUB_2W_4X 5,  3,  9102, 13623, D_8192_REG, 0, 4  ; m5=t5a, m3=t6a
    VP9_UNPACK_MULSUB_2W_4X 1,  7, 16069,  3196, D_8192_REG, 0, 4  ; m1=t4a, m7=t7a
    SUMSUB_BA            w,  5,  1, 0                       ; m5=t4a+t5a (t4), m1=t4a-t5a (t5a)
    SUMSUB_BA            w,  3,  7, 0                       ; m3=t7a+t6a (t7), m7=t7a-t6a (t6a)
%if cpuflag(ssse3)
    SUMSUB_BA            w,  1,  7, 0                       ; m1=t6a+t5a (t6), m7=t6a-t5a (t5)
    pmulhrsw            m1, W_11585x2_REG                   ; m1=t6
    pmulhrsw            m7, W_11585x2_REG                   ; m7=t5
%else
    VP9_UNPACK_MULSUB_2W_4X 7,  1, 11585, 11585, D_8192_REG, 0, 4
%endif
    VP9_UNPACK_MULSUB_2W_4X 2,  6, 15137,  6270, D_8192_REG, 0, 4  ; m2=t2a, m6=t3a

    UNSCRATCH            0, 8, blockq+ 0    ; IN(0)
    UNSCRATCH            4, 9, blockq+64    ; IN(4)
    SCRATCH              5, 8, blockq+ 0

%if cpuflag(ssse3)
    SUMSUB_BA            w, 4, 0, 5                         ; m4=IN(0)+IN(4) m0=IN(0)-IN(4)
    pmulhrsw            m4, W_11585x2_REG                   ; m4=t0a
    pmulhrsw            m0, W_11585x2_REG                   ; m0=t1a
%else
    SCRATCH              7, 9, blockq+64
    VP9_UNPACK_MULSUB_2W_4X 0,  4, 11585, 11585, D_8192_REG, 5, 7
    UNSCRATCH            7, 9, blockq+64
%endif
    SUMSUB_BA            w,  6,  4, 5                       ; m6=t0a+t3a (t0), m4=t0a-t3a (t3)
    SUMSUB_BA            w,  2,  0, 5                       ; m2=t1a+t2a (t1), m0=t1a-t2a (t2)

    VP9_IDCT8_1D_FINALIZE
%endmacro

%macro VP9_IDCT8_4x4_1D 0
    pmulhrsw            m0, W_11585x2_REG                   ; m0=t1a/t0a
    pmulhrsw            m6, m2, [pw_15137x2]                ; m6=t3a
    pmulhrsw            m2, [pw_6270x2]                     ; m2=t2a
    pmulhrsw            m7, m1, [pw_16069x2]                ; m7=t7a
    pmulhrsw            m1, [pw_3196x2]                     ; m1=t4a
    pmulhrsw            m5, m3, [pw_m9102x2]                ; m5=t5a
    pmulhrsw            m3, [pw_13623x2]                    ; m3=t6a
    SUMSUB_BA            w,  5,  1, 4                       ; m1=t4a+t5a (t4), m5=t4a-t5a (t5a)
    SUMSUB_BA            w,  3,  7, 4                       ; m3=t7a+t6a (t7), m7=t7a-t6a (t6a)
    SUMSUB_BA            w,  1,  7, 4                       ; m1=t6a+t5a (t6), m7=t6a-t5a (t5)
    pmulhrsw            m1, W_11585x2_REG                   ; m1=t6
    pmulhrsw            m7, W_11585x2_REG                   ; m7=t5
    psubw               m4, m0, m6                          ; m4=t0a-t3a (t3)
    paddw               m6, m0                              ; m6=t0a+t3a (t0)
    SCRATCH              5,  8, blockq+ 0
    SUMSUB_BA            w,  2,  0, 5                       ; m2=t1a+t2a (t1), m0=t1a-t2a (t2)
    VP9_IDCT8_1D_FINALIZE
%endmacro

%macro VP9_IDCT8_2x2_1D 1
    pmulhrsw            m0, W_11585x2_REG                   ; m0=t0
    pmulhrsw            m3, m1, W_16069x2_REG               ; m3=t7
    pmulhrsw            m1, W_3196x2_REG                    ; m1=t4
    psubw               m7, m3, m1                          ; t5 = t7a - t4a
    paddw               m5, m3, m1                          ; t6 = t7a + t4a
    pmulhrsw            m7, W_11585x2_REG                   ; m7=t5
    pmulhrsw            m5, W_11585x2_REG                   ; m5=t6
    SWAP                 5,  1
    ; merged VP9_IDCT8_1D_FINALIZE to make register-sharing w/ avx easier
    psubw               m6, m0, m3                          ; m6=t0-t7
    paddw               m3, m0                              ; m3=t0+t7
    psubw               m2, m0, m1                          ; m2=t1-t6
    paddw               m1, m0                              ; m1=t1+t6
%if %1 == 1
    punpcklwd           m3, m1
%define SCRATCH_REG 1
%elif ARCH_X86_32
    mova       [blockq+ 0], m2
%define SCRATCH_REG 2
%else
%define SCRATCH_REG 8
%endif
    psubw               m4, m0, m5                          ; m4=t3-t4
    paddw               m5, m0                              ; m5=t3+t4
    SUMSUB_BA            w,  7,  0, SCRATCH_REG             ; m7=t2+t5, m0=t2-t5
    SWAP                 7,  6,  2
    SWAP                 3,  5,  0
%undef SCRATCH_REG
%endmacro

%macro VP9_IDCT8_WRITEx2 6-8 5 ; line1, line2, tmp1, tmp2, zero, pw_1024/pw_16, shift
%if cpuflag(ssse3)
    pmulhrsw           m%1, %6              ; (x*1024 + (1<<14))>>15 <=> (x+16)>>5
    pmulhrsw           m%2, %6
%else
    paddw              m%1, %6
    paddw              m%2, %6
    psraw              m%1, %7
    psraw              m%2, %7
%endif
%if %0 <= 7
    VP9_STORE_2X        %1, %2, %3, %4, %5
%else
    VP9_STORE_2X        %1, %2, %3, %4, %5, %8
%endif
%endmacro

; x86-32:
; - m6 is in mem
; x86-64:
; - m8 holds m6 (SWAP)
; m6 holds zero
%macro VP9_IDCT8_WRITEOUT 0
%if ARCH_X86_64
%if cpuflag(ssse3)
    mova                m9, [pw_1024]
%else
    mova                m9, [pw_16]
%endif
%define ROUND_REG m9
%else
%if cpuflag(ssse3)
%define ROUND_REG [pw_1024]
%else
%define ROUND_REG [pw_16]
%endif
%endif
    SCRATCH              5, 10, blockq+16
    SCRATCH              7, 11, blockq+32
    VP9_IDCT8_WRITEx2    0,  1, 5, 7, 6, ROUND_REG
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2    2,  3, 5, 7, 6, ROUND_REG
    lea               dstq, [dstq+2*strideq]
    UNSCRATCH            5, 10, blockq+16
    UNSCRATCH            7, 11, blockq+32
    VP9_IDCT8_WRITEx2    4,  5, 0, 1, 6, ROUND_REG
    lea               dstq, [dstq+2*strideq]
    UNSCRATCH            5, 8, blockq+ 0
    VP9_IDCT8_WRITEx2    5,  7, 0, 1, 6, ROUND_REG

%undef ROUND_REG
%endmacro

%macro VP9_IDCT_IDCT_8x8_ADD_XMM 2
INIT_XMM %1
cglobal vp9_idct_idct_8x8_add, 4, 4, %2, dst, stride, block, eob

%if cpuflag(ssse3)
%if ARCH_X86_64
    mova               m12, [pw_11585x2]    ; often used
%define W_11585x2_REG m12
%else
%define W_11585x2_REG [pw_11585x2]
%endif

    cmp eobd, 12 ; top left half or less
    jg .idctfull

    cmp eobd, 3  ; top left corner or less
    jg .idcthalf

    cmp eobd, 1 ; faster path for when only DC is set
    jne .idcttopleftcorner
%else
    cmp eobd, 1
    jg .idctfull
%endif

%if cpuflag(ssse3)
    movd                m0, [blockq]
    pmulhrsw            m0, W_11585x2_REG
    pmulhrsw            m0, W_11585x2_REG
%else
    DEFINE_ARGS dst, stride, block, coef
    movsx            coefd, word [blockq]
    imul             coefd, 11585
    add              coefd, 8192
    sar              coefd, 14
    imul             coefd, 11585
    add              coefd, (16 << 14) + 8192
    sar              coefd, 14 + 5
    movd                m0, coefd
%endif
    SPLATW              m0, m0, 0
    pxor                m4, m4
    movd          [blockq], m4
%if cpuflag(ssse3)
    pmulhrsw            m0, [pw_1024]       ; (x*1024 + (1<<14))>>15 <=> (x+16)>>5
%endif
%rep 3
    VP9_STORE_2X         0,  0,  6,  7,  4
    lea               dstq, [dstq+2*strideq]
%endrep
    VP9_STORE_2X         0,  0,  6,  7,  4
    RET

%if cpuflag(ssse3)
; faster path for when only left corner is set (3 input: DC, right to DC, below
; to DC). Note: also working with a 2x2 block
.idcttopleftcorner:
    movd                m0, [blockq+0]
    movd                m1, [blockq+16]
%if ARCH_X86_64
    mova               m10, [pw_3196x2]
    mova               m11, [pw_16069x2]
%define W_3196x2_REG m10
%define W_16069x2_REG m11
%else
%define W_3196x2_REG [pw_3196x2]
%define W_16069x2_REG [pw_16069x2]
%endif
    VP9_IDCT8_2x2_1D 1
    ; partial 2x8 transpose
    ; punpcklwd m0, m1 already done inside idct
    punpcklwd           m2, m3
    punpcklwd           m4, m5
    punpcklwd           m6, m7
    punpckldq           m0, m2
    punpckldq           m4, m6
    SBUTTERFLY         qdq, 0, 4, 1
    SWAP                 1, 4
    VP9_IDCT8_2x2_1D 2
%if ARCH_X86_64
    SWAP                 6, 8
%endif
    pxor                m6, m6  ; used for the block reset, and VP9_STORE_2X
    VP9_IDCT8_WRITEOUT
%if ARCH_X86_64
    movd       [blockq+ 0], m6
    movd       [blockq+16], m6
%else
    mova       [blockq+ 0], m6
    mova       [blockq+16], m6
    mova       [blockq+32], m6
%endif
    RET

.idcthalf:
    movh                m0, [blockq + 0]
    movh                m1, [blockq +16]
    movh                m2, [blockq +32]
    movh                m3, [blockq +48]
    VP9_IDCT8_4x4_1D
    ; partial 4x8 transpose
%if ARCH_X86_32
    mova                m6, [blockq+ 0]
%endif
    punpcklwd           m0, m1
    punpcklwd           m2, m3
    punpcklwd           m4, m5
    punpcklwd           m6, m7
    SBUTTERFLY          dq, 0, 2, 1
    SBUTTERFLY          dq, 4, 6, 5
    SBUTTERFLY         qdq, 0, 4, 1
    SBUTTERFLY         qdq, 2, 6, 5
    SWAP                 1, 4
    SWAP                 3, 6
    VP9_IDCT8_4x4_1D
%if ARCH_X86_64
    SWAP                 6, 8
%endif
    pxor                m6, m6
    VP9_IDCT8_WRITEOUT
%if ARCH_X86_64
    movh       [blockq+ 0], m6
    movh       [blockq+16], m6
    movh       [blockq+32], m6
%else
    mova       [blockq+ 0], m6
    mova       [blockq+16], m6
    mova       [blockq+32], m6
%endif
    movh       [blockq+48], m6
    RET
%endif

.idctfull: ; generic full 8x8 idct/idct
%if ARCH_X86_64
    mova                m0, [blockq+  0]    ; IN(0)
%endif
    mova                m1, [blockq+ 16]    ; IN(1)
    mova                m2, [blockq+ 32]    ; IN(2)
    mova                m3, [blockq+ 48]    ; IN(3)
%if ARCH_X86_64
    mova                m4, [blockq+ 64]    ; IN(4)
%endif
    mova                m5, [blockq+ 80]    ; IN(5)
    mova                m6, [blockq+ 96]    ; IN(6)
    mova                m7, [blockq+112]    ; IN(7)
%if ARCH_X86_64
    mova               m11, [pd_8192]       ; rounding
%define D_8192_REG m11
%else
%define D_8192_REG [pd_8192]
%endif
    VP9_IDCT8_1D
%if ARCH_X86_64
    TRANSPOSE8x8W  0, 1, 2, 3, 4, 5, 6, 7, 8
%else
    TRANSPOSE8x8W  0, 1, 2, 3, 4, 5, 6, 7, [blockq+0], [blockq+64], 1
    mova        [blockq+0], m0
%endif
    VP9_IDCT8_1D

%if ARCH_X86_64
    SWAP                 6, 8
%endif
    pxor                m6, m6  ; used for the block reset, and VP9_STORE_2X
    VP9_IDCT8_WRITEOUT
    ZERO_BLOCK      blockq, 16, 8, m6
    RET
%undef W_11585x2_REG
%endmacro

VP9_IDCT_IDCT_8x8_ADD_XMM sse2, 12
VP9_IDCT_IDCT_8x8_ADD_XMM ssse3, 13
VP9_IDCT_IDCT_8x8_ADD_XMM avx, 13

;---------------------------------------------------------------------------------------------
; void vp9_iadst_iadst_8x8_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;---------------------------------------------------------------------------------------------

; x86-32:
; - in: m0/3/4/7 are in mem [blockq+N*16]
; - out: m6 is in mem [blockq+0]
; x86-64:
; - everything is in registers
%macro VP9_IADST8_1D 0 ; input/output=m0/1/2/3/4/5/6/7
%if ARCH_X86_64
    SWAP                     0, 8
    SWAP                     3, 9
    SWAP                     4, 10
    SWAP                     7, 11
%endif

    VP9_UNPACK_MULSUB_2D_4X  5,  2,  0,  3, 14449,  7723    ; m5/2=t3[d], m2/4=t2[d]
    VP9_UNPACK_MULSUB_2D_4X  1,  6,  4,  7,  4756, 15679    ; m1/4=t7[d], m6/7=t6[d]
    SCRATCH                  4, 12, blockq+1*16
    VP9_RND_SH_SUMSUB_BA     6,  2,  7,  3, 4, D_8192_REG  ; m6=t2[w], m2=t6[w]
    UNSCRATCH                4, 12, blockq+1*16
    VP9_RND_SH_SUMSUB_BA     1,  5,  4,  0, 3, D_8192_REG  ; m1=t3[w], m5=t7[w]

    UNSCRATCH                0,  8, blockq+16*0
    UNSCRATCH                3,  9, blockq+16*3
    UNSCRATCH                4, 10, blockq+16*4
    UNSCRATCH                7, 11, blockq+16*7
    SCRATCH                  1,  8, blockq+16*1
    SCRATCH                  2,  9, blockq+16*2
    SCRATCH                  5, 10, blockq+16*5
    SCRATCH                  6, 11, blockq+16*6

    VP9_UNPACK_MULSUB_2D_4X  7,  0,  1,  2, 16305,  1606    ; m7/1=t1[d], m0/2=t0[d]
    VP9_UNPACK_MULSUB_2D_4X  3,  4,  5,  6, 10394, 12665    ; m3/5=t5[d], m4/6=t4[d]
    SCRATCH                  1, 12, blockq+ 0*16
    VP9_RND_SH_SUMSUB_BA     4,  0,  6,  2, 1, D_8192_REG  ; m4=t0[w], m0=t4[w]
    UNSCRATCH                1, 12, blockq+ 0*16
    VP9_RND_SH_SUMSUB_BA     3,  7,  5,  1, 2, D_8192_REG  ; m3=t1[w], m7=t5[w]

    UNSCRATCH                2,  9, blockq+16*2
    UNSCRATCH                5, 10, blockq+16*5
    SCRATCH                  3,  9, blockq+16*3
    SCRATCH                  4, 10, blockq+16*4

    ; m4=t0, m3=t1, m6=t2, m1=t3, m0=t4, m7=t5, m2=t6, m5=t7

    VP9_UNPACK_MULSUB_2D_4X  0,  7,  1,  3, 15137,  6270    ; m0/1=t5[d], m7/3=t4[d]
    VP9_UNPACK_MULSUB_2D_4X  5,  2,  4,  6,  6270, 15137    ; m5/4=t6[d], m2/6=t7[d]
    SCRATCH                  1, 12, blockq+ 0*16
    VP9_RND_SH_SUMSUB_BA     5,  7,  4,  3, 1, D_8192_REG
    UNSCRATCH                1, 12, blockq+ 0*16
    PSIGNW                  m5, W_M1_REG                    ; m5=out1[w], m7=t6[w]
    VP9_RND_SH_SUMSUB_BA     2,  0,  6,  1, 3, D_8192_REG   ; m2=out6[w], m0=t7[w]

    UNSCRATCH                1,  8, blockq+16*1
    UNSCRATCH                3,  9, blockq+16*3
    UNSCRATCH                4, 10, blockq+16*4
    UNSCRATCH                6, 11, blockq+16*6
    SCRATCH                  2,  8, blockq+16*0

    SUMSUB_BA                w,  6,  4, 2                   ; m6=out0[w], m4=t2[w]
    SUMSUB_BA                w,  1,  3, 2
    PSIGNW                  m1, W_M1_REG                    ; m1=out7[w], m3=t3[w]

    ; m6=out0, m5=out1, m4=t2, m3=t3, m7=t6, m0=t7, m2=out6, m1=out7

    ; unfortunately, the code below overflows in some cases
%if 0; cpuflag(ssse3)
    SUMSUB_BA                w,  3,  4,  2
    SUMSUB_BA                w,  0,  7,  2
    pmulhrsw                m3, W_11585x2_REG
    pmulhrsw                m7, W_11585x2_REG
    pmulhrsw                m4, W_11585x2_REG               ; out4
    pmulhrsw                m0, W_11585x2_REG               ; out2
%else
    SCRATCH                  5,  9, blockq+16*1
    VP9_UNPACK_MULSUB_2W_4X  4, 3, 11585, 11585, D_8192_REG, 2, 5
    VP9_UNPACK_MULSUB_2W_4X  7, 0, 11585, 11585, D_8192_REG, 2, 5
    UNSCRATCH                5,  9, blockq+16*1
%endif
    PSIGNW                  m3, W_M1_REG                    ; out3
    PSIGNW                  m7, W_M1_REG                    ; out5

    ; m6=out0, m5=out1, m0=out2, m3=out3, m4=out4, m7=out5, m2=out6, m1=out7

%if ARCH_X86_64
    SWAP                     2, 8
%endif
    SWAP                     0, 6, 2
    SWAP                     7, 1, 5
%endmacro

%macro IADST8_FN 6
INIT_XMM %5
cglobal vp9_%1_%3_8x8_add, 3, 3, %6, dst, stride, block, eob

%ifidn %1, idct
%define first_is_idct 1
%else
%define first_is_idct 0
%endif

%ifidn %3, idct
%define second_is_idct 1
%else
%define second_is_idct 0
%endif

%if ARCH_X86_64
    mova                m0, [blockq+  0]    ; IN(0)
%endif
    mova                m1, [blockq+ 16]    ; IN(1)
    mova                m2, [blockq+ 32]    ; IN(2)
%if ARCH_X86_64 || first_is_idct
    mova                m3, [blockq+ 48]    ; IN(3)
%endif
%if ARCH_X86_64
    mova                m4, [blockq+ 64]    ; IN(4)
%endif
    mova                m5, [blockq+ 80]    ; IN(5)
    mova                m6, [blockq+ 96]    ; IN(6)
%if ARCH_X86_64 || first_is_idct
    mova                m7, [blockq+112]    ; IN(7)
%endif
%if ARCH_X86_64
%if cpuflag(ssse3)
    mova               m15, [pw_11585x2]    ; often used
%endif
    mova               m13, [pd_8192]       ; rounding
    mova               m14, [pw_m1]
%define W_11585x2_REG m15
%define D_8192_REG m13
%define W_M1_REG m14
%else
%define W_11585x2_REG [pw_11585x2]
%define D_8192_REG [pd_8192]
%define W_M1_REG [pw_m1]
%endif

    ; note different calling conventions for idct8 vs. iadst8 on x86-32
    VP9_%2_1D
%if ARCH_X86_64
    TRANSPOSE8x8W  0, 1, 2, 3, 4, 5, 6, 7, 8
%else
    TRANSPOSE8x8W  0, 1, 2, 3, 4, 5, 6, 7, [blockq+0], [blockq+64], 1
    mova      [blockq+  0], m0
%if second_is_idct == 0
    mova      [blockq+ 48], m3
    mova      [blockq+112], m7
%endif
%endif
    VP9_%4_1D

%if ARCH_X86_64
    SWAP                 6, 8
%endif
    pxor                m6, m6  ; used for the block reset, and VP9_STORE_2X
    VP9_IDCT8_WRITEOUT
    ZERO_BLOCK      blockq, 16, 8, m6
    RET

%undef W_11585x2_REG
%undef first_is_idct
%undef second_is_idct

%endmacro

IADST8_FN idct,  IDCT8,  iadst, IADST8, sse2, 15
IADST8_FN iadst, IADST8, idct,  IDCT8,  sse2, 15
IADST8_FN iadst, IADST8, iadst, IADST8, sse2, 15
IADST8_FN idct,  IDCT8,  iadst, IADST8, ssse3, 16
IADST8_FN idct,  IDCT8,  iadst, IADST8, avx, 16
IADST8_FN iadst, IADST8, idct,  IDCT8,  ssse3, 16
IADST8_FN iadst, IADST8, idct,  IDCT8,  avx, 16
IADST8_FN iadst, IADST8, iadst, IADST8, ssse3, 16
IADST8_FN iadst, IADST8, iadst, IADST8, avx, 16

;---------------------------------------------------------------------------------------------
; void vp9_idct_idct_16x16_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;---------------------------------------------------------------------------------------------

; x86-64:
; at the end of this macro, m7 is stored in [%4+15*%5]
; everything else (t0-6 and t8-15) is stored in m0-6 and m8-15
; the following sumsubs have not been done yet:
;    SUMSUB_BA            w,  6,  9, 15      ; t6, t9
;    SUMSUB_BA            w,  7,  8, 15      ; t7, t8
; or (x86-32) t0-t5 are in m0-m5, t10-t15 are in x11/9/7/5/3/1,
; and the following simsubs have not been done yet:
;    SUMSUB_BA            w, x13, x14, 7       ; t6, t9
;    SUMSUB_BA            w, x15, x12, 7       ; t7, t8

%macro VP9_IDCT16_1D_START 6 ; src, nnzc, stride, scratch, scratch_stride, is_iadst
%if %2 <= 4
    mova                m3, [%1+ 1*%3]      ; IN(1)
    mova                m0, [%1+ 3*%3]      ; IN(3)

    pmulhrsw            m4, m3,  [pw_16305x2]       ; t14-15
    pmulhrsw            m3, [pw_1606x2]             ; t8-9
    pmulhrsw            m7, m0,  [pw_m4756x2]       ; t10-11
    pmulhrsw            m0, [pw_15679x2]            ; t12-13

    ; m8=t0, m9=t1, m10=t2, m11=t3, m12=t4, m14=t5, m13=t6, m15=t7
    ; m3=t8, m5=t9, m1=t10, m7=t11, m0=t12, m6=t13, m2=t14, m4=t15

    VP9_UNPACK_MULSUB_2W_4X 2, 5, 4, 3, 15137,  6270, [pd_8192], 1, 6 ; t9,  t14
    SCRATCH              4, 10, %4+ 1*%5
    SCRATCH              5, 11, %4+ 7*%5
    VP9_UNPACK_MULSUB_2W_4X 6, 1, 0, 7, 6270, m15137, [pd_8192], 4, 5 ; t10, t13
    UNSCRATCH            5, 11, %4+ 7*%5

    ; m15=t0, m14=t1, m13=t2, m12=t3, m11=t4, m10=t5, m9=t6, m8=t7
    ; m7=t8, m6=t9, m2=t10, m3=t11, m4=t12, m5=t13, m1=t14, m0=t15
%else
    mova                m5, [%1+ 1*%3]      ; IN(1)
    mova                m4, [%1+ 7*%3]      ; IN(7)
%if %2 <= 8
    pmulhrsw            m2, m5,  [pw_16305x2]       ; t15
    pmulhrsw            m5, [pw_1606x2]             ; t8
    pmulhrsw            m3, m4,  [pw_m10394x2]      ; t9
    pmulhrsw            m4, [pw_12665x2]            ; t14
%else
    mova                m3, [%1+ 9*%3]      ; IN(9)
    mova                m2, [%1+15*%3]      ; IN(15)

    ; m10=in0, m5=in1, m14=in2, m6=in3, m9=in4, m7=in5, m15=in6, m4=in7
    ; m11=in8, m3=in9, m12=in10 m0=in11, m8=in12, m1=in13, m13=in14, m2=in15

    VP9_UNPACK_MULSUB_2W_4X   5,   2, 16305,  1606, [pd_8192], 0, 1 ; t8,  t15
    VP9_UNPACK_MULSUB_2W_4X   3,   4, 10394, 12665, [pd_8192], 0, 1 ; t9,  t14
%endif

    SUMSUB_BA            w,  3,  5, 0       ; t8,  t9
    SUMSUB_BA            w,  4,  2, 0       ; t15, t14

    VP9_UNPACK_MULSUB_2W_4X   2,   5, 15137,  6270, [pd_8192], 0, 1 ; t9,  t14

    SCRATCH              4, 10, %4+ 1*%5
    SCRATCH              5, 11, %4+ 7*%5

    mova                m6, [%1+ 3*%3]      ; IN(3)
    mova                m7, [%1+ 5*%3]      ; IN(5)
%if %2 <= 8
    pmulhrsw            m0, m7,  [pw_14449x2]       ; t13
    pmulhrsw            m7, [pw_7723x2]             ; t10
    pmulhrsw            m1, m6,  [pw_m4756x2]       ; t11
    pmulhrsw            m6, [pw_15679x2]            ; t12
%else
    mova                m0, [%1+11*%3]      ; IN(11)
    mova                m1, [%1+13*%3]      ; IN(13)

    VP9_UNPACK_MULSUB_2W_4X   7,   0, 14449,  7723, [pd_8192], 4, 5 ; t10, t13
    VP9_UNPACK_MULSUB_2W_4X   1,   6,  4756, 15679, [pd_8192], 4, 5 ; t11, t12
%endif

    ; m11=t0, m10=t1, m9=t2, m8=t3, m14=t4, m12=t5, m15=t6, m13=t7
    ; m5=t8, m3=t9, m7=t10, m1=t11, m6=t12, m0=t13, m4=t14, m2=t15

    SUMSUB_BA            w,  7,  1, 4       ; t11, t10
    SUMSUB_BA            w,  0,  6, 4       ; t12, t13

    ; m8=t0, m9=t1, m10=t2, m11=t3, m12=t4, m14=t5, m13=t6, m15=t7
    ; m3=t8, m5=t9, m1=t10, m7=t11, m0=t12, m6=t13, m2=t14, m4=t15

    VP9_UNPACK_MULSUB_2W_4X   6,   1, 6270, m15137, [pd_8192], 4, 5 ; t10, t13

    UNSCRATCH            5, 11, %4+ 7*%5
%endif

    ; m8=t0, m9=t1, m10=t2, m11=t3, m12=t4, m13=t5, m14=t6, m15=t7
    ; m3=t8, m2=t9, m6=t10, m7=t11, m0=t12, m1=t13, m5=t14, m4=t15

    SUMSUB_BA            w,  7,  3, 4       ; t8,  t11

    ; backup first register
    mova        [%4+15*%5], m7

    SUMSUB_BA            w,  6,  2, 7       ; t9,  t10
    UNSCRATCH            4, 10, %4+ 1*%5
    SUMSUB_BA            w,  0,  4, 7       ; t15, t12
    SUMSUB_BA            w,  1,  5, 7       ; t14. t13

    ; m15=t0, m14=t1, m13=t2, m12=t3, m11=t4, m10=t5, m9=t6, m8=t7
    ; m7=t8, m6=t9, m2=t10, m3=t11, m4=t12, m5=t13, m1=t14, m0=t15

%if cpuflag(ssse3) && %6 == 0
    SUMSUB_BA            w,  2,  5, 7
    SUMSUB_BA            w,  3,  4, 7
    pmulhrsw            m5, [pw_11585x2]    ; t10
    pmulhrsw            m4, [pw_11585x2]    ; t11
    pmulhrsw            m3, [pw_11585x2]    ; t12
    pmulhrsw            m2, [pw_11585x2]    ; t13
%else
    SCRATCH              6, 10, %4+ 1*%5
    VP9_UNPACK_MULSUB_2W_4X   5,   2, 11585, 11585, [pd_8192], 6, 7 ; t10, t13
    VP9_UNPACK_MULSUB_2W_4X   4,   3, 11585, 11585, [pd_8192], 6, 7 ; t11, t12
    UNSCRATCH            6, 10, %4+ 1*%5
%endif

    ; m15=t0, m14=t1, m13=t2, m12=t3, m11=t4, m10=t5, m9=t6, m8=t7
    ; m7=t8, m6=t9, m5=t10, m4=t11, m3=t12, m2=t13, m1=t14, m0=t15

    SCRATCH              0,  8, %4+ 1*%5
    SCRATCH              1,  9, %4+ 3*%5
    SCRATCH              2, 10, %4+ 5*%5
    SCRATCH              3, 11, %4+ 7*%5
    SCRATCH              4, 12, %4+ 9*%5
    SCRATCH              5, 13, %4+11*%5
    SCRATCH              6, 14, %4+13*%5

    ; even (tx8x8)
%if %2 <= 4
    mova                m3, [%1+ 0*%3]      ; IN(0)
    mova                m4, [%1+ 2*%3]      ; IN(2)

    pmulhrsw            m3, [pw_11585x2]    ; t0-t3
    pmulhrsw            m7, m4, [pw_16069x2]        ; t6-7
    pmulhrsw            m4, [pw_3196x2]             ; t4-5

%if 0 ; overflows :(
    paddw               m6, m7, m4
    psubw               m5, m7, m4
    pmulhrsw            m5, [pw_11585x2]            ; t5
    pmulhrsw            m6, [pw_11585x2]            ; t6
%else
    VP9_UNPACK_MULSUB_2W_4X  5, 6, 7, 4, 11585, 11585, [pd_8192], 0, 1 ; t5,  t6
%endif

    psubw               m0, m3, m7
    paddw               m7, m3
    psubw               m1, m3, m6
    paddw               m6, m3
    psubw               m2, m3, m5
    paddw               m5, m3

%if ARCH_X86_32
    SWAP                 0, 7
%endif
    SCRATCH              7, 15, %4+12*%5
%else
    mova                m6, [%1+ 2*%3]      ; IN(2)
    mova                m1, [%1+ 4*%3]      ; IN(4)
    mova                m7, [%1+ 6*%3]      ; IN(6)
%if %2 <= 8
    pmulhrsw            m0, m1,  [pw_15137x2]       ; t3
    pmulhrsw            m1, [pw_6270x2]             ; t2
    pmulhrsw            m5, m6, [pw_16069x2]        ; t7
    pmulhrsw            m6, [pw_3196x2]             ; t4
    pmulhrsw            m4, m7, [pw_m9102x2]        ; t5
    pmulhrsw            m7, [pw_13623x2]            ; t6
%else
    mova                m4, [%1+10*%3]      ; IN(10)
    mova                m0, [%1+12*%3]      ; IN(12)
    mova                m5, [%1+14*%3]      ; IN(14)

    VP9_UNPACK_MULSUB_2W_4X   1,   0, 15137,  6270, [pd_8192], 2, 3 ; t2,  t3
    VP9_UNPACK_MULSUB_2W_4X   6,   5, 16069,  3196, [pd_8192], 2, 3 ; t4,  t7
    VP9_UNPACK_MULSUB_2W_4X   4,   7,  9102, 13623, [pd_8192], 2, 3 ; t5,  t6
%endif

    SUMSUB_BA            w,  4,  6, 2       ; t4,  t5
    SUMSUB_BA            w,  7,  5, 2       ; t7,  t6

%if cpuflag(ssse3) && %6 == 0
    SUMSUB_BA            w,  6,  5, 2
    pmulhrsw            m5, [pw_11585x2]                              ; t5
    pmulhrsw            m6, [pw_11585x2]                              ; t6
%else
    VP9_UNPACK_MULSUB_2W_4X  5,  6, 11585, 11585, [pd_8192], 2, 3 ; t5,  t6
%endif

    SCRATCH              5, 15, %4+10*%5
    mova                m2, [%1+ 0*%3]      ; IN(0)
%if %2 <= 8
    pmulhrsw            m2, [pw_11585x2]    ; t0 and t1
    psubw               m3, m2, m0
    paddw               m0, m2

    SUMSUB_BA            w,  7,  0, 5       ; t0,  t7
%else
    mova                m3, [%1+ 8*%3]      ; IN(8)

    ; from 3 stages back
%if cpuflag(ssse3) && %6 == 0
    SUMSUB_BA            w,  3,  2, 5
    pmulhrsw            m3, [pw_11585x2]    ; t0
    pmulhrsw            m2, [pw_11585x2]    ; t1
%else
    mova        [%1+ 0*%3], m0
    VP9_UNPACK_MULSUB_2W_4X  2,  3, 11585,  11585, [pd_8192], 5, 0 ; t0, t1
    mova                m0, [%1+ 0*%3]
%endif

    ; from 2 stages back
    SUMSUB_BA            w,  0,  3, 5      ; t0,  t3

    SUMSUB_BA            w,  7,  0, 5      ; t0,  t7
%endif
    UNSCRATCH            5, 15, %4+10*%5
%if ARCH_X86_32
    SWAP                 0, 7
%endif
    SCRATCH              7, 15, %4+12*%5
    SUMSUB_BA            w,  1,  2, 7       ; t1,  t2

    ; from 1 stage back
    SUMSUB_BA            w,  6,  1, 7       ; t1,  t6
    SUMSUB_BA            w,  5,  2, 7       ; t2,  t5
%endif
    SUMSUB_BA            w,  4,  3, 7       ; t3,  t4

%if ARCH_X86_64
    SWAP                 0, 8
    SWAP                 1, 9
    SWAP                 2, 10
    SWAP                 3, 11
    SWAP                 4, 12
    SWAP                 5, 13
    SWAP                 6, 14

    SUMSUB_BA            w,  0, 15, 7       ; t0, t15
    SUMSUB_BA            w,  1, 14, 7       ; t1, t14
    SUMSUB_BA            w,  2, 13, 7       ; t2, t13
    SUMSUB_BA            w,  3, 12, 7       ; t3, t12
    SUMSUB_BA            w,  4, 11, 7       ; t4, t11
    SUMSUB_BA            w,  5, 10, 7       ; t5, t10
%else
    SWAP                 1, 6
    SWAP                 2, 5
    SWAP                 3, 4
    mova        [%4+14*%5], m6

%macro %%SUMSUB_BA_STORE 5 ; reg, from_mem, to_mem, scratch, scratch_stride
    mova                m6, [%4+%2*%5]
    SUMSUB_BA            w,  6, %1, 7
    SWAP                %1, 6
    mova        [%4+%3*%5], m6
%endmacro

    %%SUMSUB_BA_STORE    0,  1,  1, %4, %5  ; t0, t15
    %%SUMSUB_BA_STORE    1,  3,  3, %4, %5  ; t1, t14
    %%SUMSUB_BA_STORE    2,  5,  5, %4, %5  ; t2, t13
    %%SUMSUB_BA_STORE    3,  7,  7, %4, %5  ; t3, t12
    %%SUMSUB_BA_STORE    4,  9,  9, %4, %5  ; t4, t11
    %%SUMSUB_BA_STORE    5, 11, 11, %4, %5  ; t5, t10
%endif
%endmacro

%macro VP9_IDCT16_1D 2-4 16, 1 ; src, pass, nnzc, is_iadst
%if %2 == 1
    VP9_IDCT16_1D_START %1, %3, 32, tmpq, 16, %4

%if ARCH_X86_64
    ; backup a different register
    mova                m7, [tmpq+15*16]
    mova      [tmpq+ 1*16], m15

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

    mova               m15, [tmpq+ 1*16]
    TRANSPOSE8x8W        8, 9, 10, 11, 12, 13, 14, 15, 0
    mova        [tmpq+ 16], m8
    mova        [tmpq+ 48], m9
    mova        [tmpq+ 80], m10
    mova        [tmpq+112], m11
    mova        [tmpq+144], m12
    mova        [tmpq+176], m13
    mova        [tmpq+208], m14
    mova        [tmpq+240], m15
%else
    mova                m6, [tmpq+13*16]
    mova                m7, [tmpq+14*16]
    SUMSUB_BA            w, 6, 7                ; t6, t9
    mova      [tmpq+14*16], m6
    mova      [tmpq+13*16], m7
    mova                m7, [tmpq+15*16]
    mova                m6, [tmpq+12*16]
    SUMSUB_BA            w, 7, 6                ; t7, t8
    mova      [tmpq+15*16], m6

    TRANSPOSE8x8W       0, 1, 2, 3, 4, 5, 6, 7, [tmpq+14*16], [tmpq+ 8*16], 1
    mova     [tmpq+ 0*16], m0
    mova     [tmpq+ 2*16], m1
    mova     [tmpq+ 4*16], m2
    mova     [tmpq+ 6*16], m3
    mova     [tmpq+10*16], m5
    mova     [tmpq+12*16], m6
    mova     [tmpq+14*16], m7

    mova                m0, [tmpq+15*16]
    mova                m1, [tmpq+13*16]
    mova                m2, [tmpq+11*16]
    mova                m3, [tmpq+ 9*16]
    mova                m4, [tmpq+ 7*16]
    mova                m5, [tmpq+ 5*16]
    mova                m7, [tmpq+ 1*16]
    TRANSPOSE8x8W       0, 1, 2, 3, 4, 5, 6, 7, [tmpq+ 3*16], [tmpq+ 9*16], 1
    mova     [tmpq+ 1*16], m0
    mova     [tmpq+ 3*16], m1
    mova     [tmpq+ 5*16], m2
    mova     [tmpq+ 7*16], m3
    mova     [tmpq+11*16], m5
    mova     [tmpq+13*16], m6
    mova     [tmpq+15*16], m7
%endif
%else ; %2 == 2
    VP9_IDCT16_1D_START %1, %3, 32, %1, 32, %4

%if cpuflag(ssse3)
%define ROUND_REG [pw_512]
%else
%define ROUND_REG [pw_32]
%endif

    pxor                m7, m7
%if ARCH_X86_64
    ; backup more registers
    mova        [%1+ 2*32], m8
    mova        [%1+ 3*32], m9

    VP9_IDCT8_WRITEx2    0,  1, 8, 9, 7, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2    2,  3, 8, 9, 7, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2    4,  5, 8, 9, 7, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]

    ; restore from cache
    SWAP                 0, 7               ; move zero from m7 to m0
    mova                m7, [%1+15*32]
    mova                m8, [%1+ 2*32]
    mova                m9, [%1+ 3*32]

    SUMSUB_BA            w,  6,  9, 3       ; t6, t9
    SUMSUB_BA            w,  7,  8, 3       ; t7, t8

    VP9_IDCT8_WRITEx2    6,  7, 3, 4, 0, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2    8,  9, 3, 4, 0, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2   10, 11, 1, 2, 0, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2   12, 13, 1, 2, 0, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2   14, 15, 1, 2, 0, ROUND_REG, 6
%else
    mova      [tmpq+ 0*32], m5

    VP9_IDCT8_WRITEx2    0,  1, 5, 6, 7, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2    2,  3, 5, 6, 7, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]

    SWAP                 0, 7               ; move zero from m7 to m0
    mova                m5, [tmpq+ 0*32]

    VP9_IDCT8_WRITEx2    4,  5, 1, 2, 0, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]

    mova                m4, [tmpq+13*32]
    mova                m7, [tmpq+14*32]
    mova                m5, [tmpq+15*32]
    mova                m6, [tmpq+12*32]
    SUMSUB_BADC w, 4, 7, 5, 6, 1

    VP9_IDCT8_WRITEx2    4,  5, 1, 2, 0, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2    6,  7, 1, 2, 0, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]

    mova                m4, [tmpq+11*32]
    mova                m5, [tmpq+ 9*32]
    mova                m6, [tmpq+ 7*32]
    mova                m7, [tmpq+ 5*32]

    VP9_IDCT8_WRITEx2    4,  5, 1, 2, 0, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2    6,  7, 1, 2, 0, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]

    mova                m4, [tmpq+ 3*32]
    mova                m5, [tmpq+ 1*32]

    VP9_IDCT8_WRITEx2    4,  5, 1, 2, 0, ROUND_REG, 6
    lea               dstq, [dstq+strideq*2]
%endif

%undef ROUND_REG
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
%if cpuflag(ssse3)
    ; 2x2=eob=3, 4x4=eob=10
    cmp eobd, 38
    jg .idctfull
    cmp eobd, 1 ; faster path for when only DC is set
    jne .idct8x8
%else
    cmp eobd, 1 ; faster path for when only DC is set
    jg .idctfull
%endif

    ; dc-only
%if cpuflag(ssse3)
    movd                m0, [blockq]
    mova                m1, [pw_11585x2]
    pmulhrsw            m0, m1
    pmulhrsw            m0, m1
%else
    DEFINE_ARGS dst, stride, block, coef
    movsx            coefd, word [blockq]
    imul             coefd, 11585
    add              coefd, 8192
    sar              coefd, 14
    imul             coefd, 11585
    add              coefd, (32 << 14) + 8192
    sar              coefd, 14 + 6
    movd                m0, coefd
%endif
    SPLATW              m0, m0, q0000
%if cpuflag(ssse3)
    pmulhrsw            m0, [pw_512]
%endif
    pxor                m5, m5
    movd          [blockq], m5
%rep 7
    VP9_STORE_2XFULL    0, 1, 2, 3, 4, 5
    lea               dstq, [dstq+2*strideq]
%endrep
    VP9_STORE_2XFULL    0, 1, 2, 3, 4, 5
    RET

    DEFINE_ARGS dst, stride, block, cnt, dst_bak, tmp
%if cpuflag(ssse3)
.idct8x8:
    mov               tmpq, rsp
    VP9_IDCT16_1D   blockq, 1, 8, 0

    mov               cntd, 2
    mov           dst_bakq, dstq
.loop2_8x8:
    VP9_IDCT16_1D     tmpq, 2, 8, 0
    lea               dstq, [dst_bakq+8]
    add               tmpq, 16
    dec               cntd
    jg .loop2_8x8

    ; at the end of the loop, m0 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 32, 8, m0
    RET
%endif

.idctfull:
    mov               cntd, 2
    mov               tmpq, rsp
.loop1_full:
    VP9_IDCT16_1D   blockq, 1, 16, 0
    add             blockq, 16
    add               tmpq, 256
    dec               cntd
    jg .loop1_full
    sub             blockq, 32

    mov               cntd, 2
    mov               tmpq, rsp
    mov           dst_bakq, dstq
.loop2_full:
    VP9_IDCT16_1D     tmpq, 2, 16, 0
    lea               dstq, [dst_bakq+8]
    add               tmpq, 16
    dec               cntd
    jg .loop2_full

    ; at the end of the loop, m0 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 32, 16, m0
    RET
%endmacro

VP9_IDCT_IDCT_16x16_ADD_XMM sse2
VP9_IDCT_IDCT_16x16_ADD_XMM ssse3
VP9_IDCT_IDCT_16x16_ADD_XMM avx

%macro VP9_IDCT16_YMM_1D 0
    VP9_UNPACK_MULSUB_2W_4X  1,  15, 16305,  1606, [pd_8192], 0, 4 ; t8,  t15
    VP9_UNPACK_MULSUB_2W_4X  9,   7, 10394, 12665, [pd_8192], 0, 4 ; t9,  t14

    SUMSUB_BA            w,  9,   1, 0      ; t8,  t9
    SUMSUB_BA            w,  7,  15, 0      ; t15, t14

    VP9_UNPACK_MULSUB_2W_4X 15,   1, 15137,  6270, [pd_8192], 0, 4 ; t9,  t14

    VP9_UNPACK_MULSUB_2W_4X  5,  11, 14449,  7723, [pd_8192], 0, 4 ; t10, t13
    VP9_UNPACK_MULSUB_2W_4X 13,   3,  4756, 15679, [pd_8192], 0, 4 ; t11, t12

    SUMSUB_BA            w,  5,  13, 0      ; t11, t10
    SUMSUB_BA            w, 11,   3, 0      ; t12, t13

    VP9_UNPACK_MULSUB_2W_4X  3,  13, 6270, m15137, [pd_8192], 0, 4 ; t10, t13

    SUMSUB_BA            w,  5,   9, 0      ; t8,  t11
    SUMSUB_BA            w,  3,  15, 0      ; t9,  t10
    SUMSUB_BA            w, 11,   7, 0      ; t15, t12
    SUMSUB_BA            w, 13,   1, 0      ; t14, t13

    SUMSUB_BA            w, 15,   1, 0
    SUMSUB_BA            w,  9,   7, 0
    pmulhrsw            m1, [pw_11585x2]    ; t10
    pmulhrsw            m7, [pw_11585x2]    ; t11
    pmulhrsw            m9, [pw_11585x2]    ; t12
    pmulhrsw           m15, [pw_11585x2]    ; t13

    ; even (tx8x8)
    mova                m4, [blockq+128]
    mova      [blockq+128], m5
    VP9_UNPACK_MULSUB_2W_4X   4,  12, 15137,  6270, [pd_8192], 0, 5 ; t2,  t3
    VP9_UNPACK_MULSUB_2W_4X   2,  14, 16069,  3196, [pd_8192], 0, 5 ; t4,  t7
    VP9_UNPACK_MULSUB_2W_4X  10,   6,  9102, 13623, [pd_8192], 0, 5 ; t5,  t6
    mova                m0, [blockq+  0]
    SUMSUB_BA            w,   8,   0, 5
    pmulhrsw            m8, [pw_11585x2]    ; t0
    pmulhrsw            m0, [pw_11585x2]    ; t1

    SUMSUB_BA            w,  10,   2, 5     ; t4,  t5
    SUMSUB_BA            w,   6,  14, 5     ; t7,  t6
    SUMSUB_BA            w,  12,   8, 5     ; t0,  t3
    SUMSUB_BA            w,   4,   0, 5     ; t1,  t2

    SUMSUB_BA            w,   2,  14, 5
    pmulhrsw           m14, [pw_11585x2]    ; t5
    pmulhrsw            m2, [pw_11585x2]    ; t6

    SUMSUB_BA            w,   6,  12, 5     ; t0,  t7
    SUMSUB_BA            w,   2,   4, 5     ; t1,  t6
    SUMSUB_BA            w,  14,   0, 5     ; t2,  t5
    SUMSUB_BA            w,  10,   8, 5     ; t3,  t4

    ; final stage
    SUMSUB_BA            w, 11,  6,  5      ; out0, out15
    SUMSUB_BA            w, 13,  2,  5      ; out1, out14
    SUMSUB_BA            w, 15, 14,  5      ; out2, out13
    SUMSUB_BA            w,  9, 10,  5      ; out3, out12
    SUMSUB_BA            w,  7,  8,  5      ; out4, out11
    SUMSUB_BA            w,  1,  0,  5      ; out5, out10
    SUMSUB_BA            w,  3,  4,  5      ; out6, out9
    mova                m5, [blockq+128]
    mova      [blockq+192], m3
    SUMSUB_BA            w,  5, 12,  3      ; out7, out8

    SWAP  0, 11,  8, 12, 10
    SWAP  1, 13, 14,  2, 15,  6,  3,  9,  4,  7,  5
%endmacro

; this is almost identical to VP9_STORE_2X, but it does two rows
; for slightly improved interleaving, and it omits vpermq since the
; input is DC so all values are identical
%macro VP9_STORE_YMM_DC_4X 6 ; reg, tmp1, tmp2, tmp3, tmp4, zero
    mova              xm%2, [dstq]
    mova              xm%4, [dstq+strideq*2]
    vinserti128        m%2, m%2, [dstq+strideq], 1
    vinserti128        m%4, m%4, [dstq+stride3q], 1
    punpckhbw          m%3, m%2, m%6
    punpcklbw          m%2, m%6
    punpckhbw          m%5, m%4, m%6
    punpcklbw          m%4, m%6
    paddw              m%3, m%1
    paddw              m%2, m%1
    paddw              m%5, m%1
    paddw              m%4, m%1
    packuswb           m%2, m%3
    packuswb           m%4, m%5
    mova            [dstq], xm%2
    mova        [dstq+strideq*2], xm%4
    vextracti128  [dstq+strideq], m%2, 1
    vextracti128 [dstq+stride3q], m%4, 1
%endmacro

%if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
INIT_YMM avx2
cglobal vp9_idct_idct_16x16_add, 4, 4, 16, dst, stride, block, eob
    cmp eobd, 1 ; faster path for when only DC is set
    jg .idctfull

    ; dc-only
    mova                m1, [pw_11585x2]
    vpbroadcastw        m0, [blockq]
    pmulhrsw            m0, m1
    pmulhrsw            m0, m1
    pxor                m5, m5
    pmulhrsw            m0, [pw_512]
    movd          [blockq], xm5

    DEFINE_ARGS dst, stride, stride3, cnt
    mov               cntd, 4
    lea           stride3q, [strideq*3]
.loop_dc:
    VP9_STORE_YMM_DC_4X  0, 1, 2, 3, 4, 5
    lea               dstq, [dstq+4*strideq]
    dec               cntd
    jg .loop_dc
    RET

    DEFINE_ARGS dst, stride, block, eob
.idctfull:
    mova                m1, [blockq+ 32]
    mova                m2, [blockq+ 64]
    mova                m3, [blockq+ 96]
    mova                m5, [blockq+160]
    mova                m6, [blockq+192]
    mova                m7, [blockq+224]
    mova                m8, [blockq+256]
    mova                m9, [blockq+288]
    mova               m10, [blockq+320]
    mova               m11, [blockq+352]
    mova               m12, [blockq+384]
    mova               m13, [blockq+416]
    mova               m14, [blockq+448]
    mova               m15, [blockq+480]

    VP9_IDCT16_YMM_1D
    TRANSPOSE16x16W      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, \
                         [blockq+192], [blockq+128], 1
    mova      [blockq+  0], m0
    VP9_IDCT16_YMM_1D

    mova      [blockq+224], m7

    ; store
    VP9_IDCT8_WRITEx2    0,  1, 6, 7, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2    2,  3, 6, 7, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2    4,  5, 6, 7, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    mova                m6, [blockq+192]
    mova                m7, [blockq+224]
    VP9_IDCT8_WRITEx2    6,  7, 1, 2, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2    8,  9, 1, 2, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2   10, 11, 1, 2, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2   12, 13, 1, 2, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2   14, 15, 1, 2, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]

    ; at the end of the loop, m0 should still be zero
    ; use that to zero out block coefficients
    pxor                m0, m0
    ZERO_BLOCK      blockq, 32, 16, m0
    RET
%endif

;---------------------------------------------------------------------------------------------
; void vp9_iadst_iadst_16x16_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;---------------------------------------------------------------------------------------------

%macro VP9_IADST16_1D 2 ; src, pass
%assign %%str 16*%2
    mova                m0, [%1+ 0*32]  ; in0
    mova                m1, [%1+15*32]  ; in15
    mova                m2, [%1+ 7*32]  ; in7
    mova                m3, [%1+ 8*32]  ; in8

    VP9_UNPACK_MULSUB_2D_4X  1,  0,  4,  5, 16364,   804    ; m1/4=t1[d], m0/5=t0[d]
    VP9_UNPACK_MULSUB_2D_4X  2,  3,  7,  6, 11003, 12140    ; m2/7=t9[d], m3/6=t8[d]
    SCRATCH              4, 8, tmpq+ 0*%%str
    VP9_RND_SH_SUMSUB_BA     3,  0,  6,  5,  4, [pd_8192]   ; m3=t0[w], m0=t8[w]
    UNSCRATCH            4, 8, tmpq+ 0*%%str
    VP9_RND_SH_SUMSUB_BA     2,  1,  7,  4,  5, [pd_8192]   ; m2=t1[w], m1=t9[w]

    SCRATCH              0, 10, tmpq+ 0*%%str
    SCRATCH              1, 11, tmpq+15*%%str
    mova   [tmpq+ 7*%%str], m2
    mova   [tmpq+ 8*%%str], m3

    mova                m1, [%1+ 2*32]  ; in2
    mova                m0, [%1+13*32]  ; in13
    mova                m3, [%1+ 5*32]  ; in5
    mova                m2, [%1+10*32]  ; in10

    VP9_UNPACK_MULSUB_2D_4X  0,  1,  6,  7, 15893,  3981    ; m0/6=t3[d], m1/7=t2[d]
    VP9_UNPACK_MULSUB_2D_4X  3,  2,  4,  5,  8423, 14053    ; m3/4=t11[d], m2/5=t10[d]
    SCRATCH              4, 12, tmpq+ 2*%%str
    VP9_RND_SH_SUMSUB_BA     2,  1,  5,  7,  4, [pd_8192]   ; m2=t2[w], m1=t10[w]
    UNSCRATCH            4, 12, tmpq+ 2*%%str
    VP9_RND_SH_SUMSUB_BA     3,  0,  4,  6,  5, [pd_8192]   ; m3=t3[w], m0=t11[w]

    SCRATCH              0, 12, tmpq+ 2*%%str
    SCRATCH              1, 13, tmpq+13*%%str
    mova   [tmpq+ 5*%%str], m2
    mova   [tmpq+10*%%str], m3

    mova                m2, [%1+ 4*32]  ; in4
    mova                m3, [%1+11*32]  ; in11
    mova                m0, [%1+ 3*32]  ; in3
    mova                m1, [%1+12*32]  ; in12

    VP9_UNPACK_MULSUB_2D_4X  3,  2,  7,  6, 14811,  7005    ; m3/7=t5[d], m2/6=t4[d]
    VP9_UNPACK_MULSUB_2D_4X  0,  1,  4,  5,  5520, 15426    ; m0/4=t13[d], m1/5=t12[d]
    SCRATCH              4, 9, tmpq+ 4*%%str
    VP9_RND_SH_SUMSUB_BA     1,  2,  5,  6,  4, [pd_8192]   ; m1=t4[w], m2=t12[w]
    UNSCRATCH            4, 9, tmpq+ 4*%%str
    VP9_RND_SH_SUMSUB_BA     0,  3,  4,  7,  6, [pd_8192]   ; m0=t5[w], m3=t13[w]

    SCRATCH              0,  8, tmpq+ 4*%%str
    mova   [tmpq+11*%%str], m1          ; t4:m1->r11
    UNSCRATCH            0, 10, tmpq+ 0*%%str
    UNSCRATCH            1, 11, tmpq+15*%%str

    ; round 2 interleaved part 1
    VP9_UNPACK_MULSUB_2D_4X  0,  1,  6,  7, 16069,  3196    ; m1/7=t8[d], m0/6=t9[d]
    VP9_UNPACK_MULSUB_2D_4X  3,  2,  5,  4,  3196, 16069    ; m3/5=t12[d], m2/4=t13[d]
    SCRATCH              4, 9, tmpq+ 3*%%str
    VP9_RND_SH_SUMSUB_BA     3,  1,  5,  7,  4, [pd_8192]   ; m3=t8[w], m1=t12[w]
    UNSCRATCH            4, 9, tmpq+ 3*%%str
    VP9_RND_SH_SUMSUB_BA     2,  0,  4,  6,  5, [pd_8192]   ; m2=t9[w], m0=t13[w]

    SCRATCH              0, 10, tmpq+ 0*%%str
    SCRATCH              1, 11, tmpq+15*%%str
    SCRATCH              2, 14, tmpq+ 3*%%str
    SCRATCH              3, 15, tmpq+12*%%str

    mova                m2, [%1+ 6*32]  ; in6
    mova                m3, [%1+ 9*32]  ; in9
    mova                m0, [%1+ 1*32]  ; in1
    mova                m1, [%1+14*32]  ; in14

    VP9_UNPACK_MULSUB_2D_4X  3,  2,  7,  6, 13160,  9760    ; m3/7=t7[d], m2/6=t6[d]
    VP9_UNPACK_MULSUB_2D_4X  0,  1,  4,  5,  2404, 16207    ; m0/4=t15[d], m1/5=t14[d]
    SCRATCH              4, 9, tmpq+ 6*%%str
    VP9_RND_SH_SUMSUB_BA     1,  2,  5,  6,  4, [pd_8192]   ; m1=t6[w], m2=t14[w]
    UNSCRATCH            4, 9, tmpq+ 6*%%str
    VP9_RND_SH_SUMSUB_BA     0,  3,  4,  7,  6, [pd_8192]   ; m0=t7[w], m3=t15[w]

    ; r8=t0, r7=t1, r5=t2, r10=t3, r11=t4, m8|r4=t5, m1=t6, m0=t7
    ; m10|r0=t8, m11|r15=t9, m13|r13=t10, m12|r2=t11, m14|r3=t12, m15|r12=t13, m2=t14, m3=t15

    UNSCRATCH            4, 12, tmpq+ 2*%%str
    UNSCRATCH            5, 13, tmpq+13*%%str
    SCRATCH              0, 12, tmpq+ 1*%%str
    SCRATCH              1, 13, tmpq+14*%%str

    ; remainder of round 2 (rest of t8-15)
    VP9_UNPACK_MULSUB_2D_4X  5,  4,  6,  7,  9102, 13623    ; m5/6=t11[d], m4/7=t10[d]
    VP9_UNPACK_MULSUB_2D_4X  3,  2,  1,  0, 13623,  9102    ; m3/1=t14[d], m2/0=t15[d]
    SCRATCH              0, 9, tmpq+ 6*%%str
    VP9_RND_SH_SUMSUB_BA     3,  4,  1,  7,  0, [pd_8192]   ; m3=t10[w], m4=t14[w]
    UNSCRATCH            0, 9, tmpq+ 6*%%str
    VP9_RND_SH_SUMSUB_BA     2,  5,  0,  6,  1, [pd_8192]   ; m2=t11[w], m5=t15[w]

    ; m15|r12=t8, m14|r3=t9, m3=t10, m2=t11, m11|r15=t12, m10|r0=t13, m4=t14, m5=t15

    UNSCRATCH            6, 14, tmpq+ 3*%%str
    UNSCRATCH            7, 15, tmpq+12*%%str

    SUMSUB_BA                w,  3,  7,  1
    PSIGNW                  m3, [pw_m1]                     ; m3=out1[w], m7=t10[w]
    SUMSUB_BA                w,  2,  6,  1                  ; m2=out14[w], m6=t11[w]

    ; unfortunately, the code below overflows in some cases, e.g.
    ; http://downloads.webmproject.org/test_data/libvpx/vp90-2-14-resize-fp-tiles-16-8.webm
%if 0; cpuflag(ssse3)
    SUMSUB_BA                w,  7,  6,  1
    pmulhrsw                m7, [pw_11585x2]                ; m7=out6[w]
    pmulhrsw                m6, [pw_11585x2]                ; m6=out9[w]
%else
    VP9_UNPACK_MULSUB_2W_4X  6,  7, 11585, 11585, [pd_8192], 1, 0
%endif

    mova       [tmpq+ 3*%%str], m6
    mova       [tmpq+ 6*%%str], m7
    UNSCRATCH                6, 10, tmpq+ 0*%%str
    UNSCRATCH                7, 11, tmpq+15*%%str
    mova       [tmpq+13*%%str], m2
    SCRATCH                  3, 11, tmpq+ 9*%%str

    VP9_UNPACK_MULSUB_2D_4X  7,  6,  2,  3, 15137,  6270    ; m6/3=t13[d], m7/2=t12[d]
    VP9_UNPACK_MULSUB_2D_4X  5,  4,  1,  0,  6270, 15137    ; m5/1=t14[d], m4/0=t15[d]
    SCRATCH              0, 9, tmpq+ 2*%%str
    VP9_RND_SH_SUMSUB_BA     5,  6,  1,  3,  0, [pd_8192]   ; m5=out2[w], m6=t14[w]
    UNSCRATCH            0, 9, tmpq+ 2*%%str
    VP9_RND_SH_SUMSUB_BA     4,  7,  0,  2,  1, [pd_8192]
    PSIGNW                  m4, [pw_m1]                     ; m4=out13[w], m7=t15[w]

    ; unfortunately, the code below overflows in some cases
%if 0; cpuflag(ssse3)
    SUMSUB_BA                w,  7,  6,  1
    pmulhrsw                m7, [pw_m11585x2]               ; m7=out5[w]
    pmulhrsw                m6, [pw_11585x2]                ; m6=out10[w]
%else
    PSIGNW                  m7, [pw_m1]
    VP9_UNPACK_MULSUB_2W_4X  7,  6, 11585, 11585, [pd_8192], 1, 0
%endif

    ; m11|r13=out1, m5=out2, m7=out5, r15=out6, r3=out9, m6=out10, m4=out13, r2=out14

    mova                    m2, [tmpq+ 8*%%str]
    mova                    m3, [tmpq+ 7*%%str]
    mova                    m1, [tmpq+11*%%str]
    mova       [tmpq+ 7*%%str], m6
    mova       [tmpq+11*%%str], m4
    mova                    m4, [tmpq+ 5*%%str]
    SCRATCH                  5, 14, tmpq+ 5*%%str
    SCRATCH                  7, 15, tmpq+ 8*%%str
    UNSCRATCH                6,  8, tmpq+ 4*%%str
    UNSCRATCH                5, 12, tmpq+ 1*%%str
    UNSCRATCH                7, 13, tmpq+14*%%str

    ; m2=t0, m3=t1, m9=t2, m0=t3, m1=t4, m8=t5, m13=t6, m12=t7
    ; m11|r13=out1, m5=out2, m7=out5, r15=out6, r3=out9, r10=out10, r11=out13, r2=out14

    SUMSUB_BA                w,  1,  2, 0                   ; m1=t0[w], m2=t4[w]
    mova                    m0, [tmpq+10*%%str]
    SCRATCH                  1, 12, tmpq+ 1*%%str
    SUMSUB_BA                w,  6,  3, 1                   ; m8=t1[w], m3=t5[w]
    SCRATCH                  6, 13, tmpq+ 4*%%str
    SUMSUB_BA                w,  7,  4, 1                   ; m13=t2[w], m9=t6[w]
    SCRATCH                  7,  8, tmpq+10*%%str
    SUMSUB_BA                w,  5,  0, 1                   ; m12=t3[w], m0=t7[w]
    SCRATCH                  5,  9, tmpq+14*%%str

    VP9_UNPACK_MULSUB_2D_4X  2,  3,  7,  5, 15137,  6270    ; m2/6=t5[d], m3/10=t4[d]
    VP9_UNPACK_MULSUB_2D_4X  0,  4,  1,  6,  6270, 15137    ; m0/14=t6[d], m9/15=t7[d]
    SCRATCH                  6, 10, tmpq+ 0*%%str
    VP9_RND_SH_SUMSUB_BA     0,  3,  1,  5,  6, [pd_8192]
    UNSCRATCH                6, 10, tmpq+ 0*%%str
    PSIGNW                  m0, [pw_m1]                     ; m0=out3[w], m3=t6[w]
    VP9_RND_SH_SUMSUB_BA     4,  2,  6,  7,  5, [pd_8192]   ; m9=out12[w], m2=t7[w]

    UNSCRATCH                1,  8, tmpq+10*%%str
    UNSCRATCH                5,  9, tmpq+14*%%str
    UNSCRATCH                6, 12, tmpq+ 1*%%str
    UNSCRATCH                7, 13, tmpq+ 4*%%str
    SCRATCH                  4,  9, tmpq+14*%%str

    SUMSUB_BA                w,  1,  6,  4                  ; m13=out0[w], m1=t2[w]
    SUMSUB_BA                w,  5,  7,  4
    PSIGNW                  m5, [pw_m1]                     ; m12=out15[w], m8=t3[w]

    ; unfortunately, the code below overflows in some cases, e.g.
    ; http://downloads.webmproject.org/test_data/libvpx/vp90-2-14-resize-fp-tiles-16-8-4-2-1.webm
%if 0 ; cpuflag(ssse3)
    SUMSUB_BA               w,   7,  6,  4
    pmulhrsw                m7, [pw_m11585x2]               ; m8=out7[w]
    pmulhrsw                m6, [pw_11585x2]                ; m1=out8[w]
    SWAP                     6,  7
    SUMSUB_BA                w,  3,  2,  4
    pmulhrsw                m3, [pw_11585x2]                ; m3=out4[w]
    pmulhrsw                m2, [pw_11585x2]                ; m2=out11[w]
%else
    SCRATCH                  5,  8, tmpq+10*%%str
    VP9_UNPACK_MULSUB_2W_4X  6,  7, 11585, m11585, [pd_8192],  5,  4
    VP9_UNPACK_MULSUB_2W_4X  2,  3, 11585, 11585, [pd_8192],  5,  4
    UNSCRATCH                5,  8, tmpq+10*%%str
%endif

    ; m13=out0, m0=out3, m3=out4, m8=out7, m1=out8, m2=out11, m9=out12, m12=out15
    ; m11|r13=out1, m5=out2, m7=out5, r15=out6, r3=out9, r10=out10, r11=out13, r2=out14

%if %2 == 1
%if ARCH_X86_64
    mova                   m13, [tmpq+ 6*%%str]
    TRANSPOSE8x8W            1, 11, 14, 0, 3, 15, 13, 6, 10
    mova          [tmpq+ 0*16], m1
    mova          [tmpq+ 2*16], m11
    mova          [tmpq+ 4*16], m14
    mova          [tmpq+ 6*16], m0
    mova                    m1, [tmpq+ 3*%%str]
    mova                   m11, [tmpq+ 7*%%str]
    mova                   m14, [tmpq+11*%%str]
    mova                    m0, [tmpq+13*%%str]
    mova          [tmpq+ 8*16], m3
    mova          [tmpq+10*16], m15
    mova          [tmpq+12*16], m13
    mova          [tmpq+14*16], m6

    TRANSPOSE8x8W            7, 1, 11, 2, 9, 14, 0, 5, 10
    mova          [tmpq+ 1*16], m7
    mova          [tmpq+ 3*16], m1
    mova          [tmpq+ 5*16], m11
    mova          [tmpq+ 7*16], m2
    mova          [tmpq+ 9*16], m9
    mova          [tmpq+11*16], m14
    mova          [tmpq+13*16], m0
    mova          [tmpq+15*16], m5
%else
    mova       [tmpq+12*%%str], m2
    mova       [tmpq+ 1*%%str], m5
    mova       [tmpq+15*%%str], m7
    mova                    m2, [tmpq+ 9*%%str]
    mova                    m5, [tmpq+ 5*%%str]
    mova                    m7, [tmpq+ 8*%%str]
    TRANSPOSE8x8W            1, 2, 5, 0, 3, 7, 4, 6, [tmpq+ 6*%%str], [tmpq+ 8*%%str], 1
    mova          [tmpq+ 0*16], m1
    mova          [tmpq+ 2*16], m2
    mova          [tmpq+ 4*16], m5
    mova          [tmpq+ 6*16], m0
    mova          [tmpq+10*16], m7
    mova                    m3, [tmpq+12*%%str]
    mova          [tmpq+12*16], m4
    mova                    m4, [tmpq+14*%%str]
    mova          [tmpq+14*16], m6

    mova                    m0, [tmpq+15*%%str]
    mova                    m1, [tmpq+ 3*%%str]
    mova                    m2, [tmpq+ 7*%%str]
    mova                    m5, [tmpq+11*%%str]
    mova                    m7, [tmpq+ 1*%%str]
    TRANSPOSE8x8W            0, 1, 2, 3, 4, 5, 6, 7, [tmpq+13*%%str], [tmpq+ 9*%%str], 1
    mova          [tmpq+ 1*16], m0
    mova          [tmpq+ 3*16], m1
    mova          [tmpq+ 5*16], m2
    mova          [tmpq+ 7*16], m3
    mova          [tmpq+11*16], m5
    mova          [tmpq+13*16], m6
    mova          [tmpq+15*16], m7
%endif
%else
    pxor                    m4, m4

%if cpuflag(ssse3)
%define ROUND_REG [pw_512]
%else
%define ROUND_REG [pw_32]
%endif

%if ARCH_X86_64
    mova                   m12, [tmpq+ 6*%%str]
    VP9_IDCT8_WRITEx2        1, 11, 10,  8,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2       14,  0, 10,  8,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2        3, 15, 10,  8,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2       12,  6, 10,  8,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]

    mova                    m1, [tmpq+ 3*%%str]
    mova                   m11, [tmpq+ 7*%%str]
    mova                   m14, [tmpq+11*%%str]
    mova                    m0, [tmpq+13*%%str]

    VP9_IDCT8_WRITEx2        7,  1, 10,  8,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2       11,  2, 10,  8,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2        9, 14, 10,  8,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    VP9_IDCT8_WRITEx2        0,  5, 10,  8,  4, ROUND_REG, 6
%else
    mova       [tmpq+ 0*%%str], m2
    mova       [tmpq+ 1*%%str], m5
    mova       [tmpq+ 2*%%str], m7
    mova                    m2, [tmpq+ 9*%%str]
    VP9_IDCT8_WRITEx2        1,  2,  5,  7,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    mova                    m5, [tmpq+ 5*%%str]
    VP9_IDCT8_WRITEx2        5,  0,  1,  2,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    mova                    m5, [tmpq+ 8*%%str]
    VP9_IDCT8_WRITEx2        3,  5,  1,  2,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    mova                    m5, [tmpq+ 6*%%str]
    VP9_IDCT8_WRITEx2        5,  6,  1,  2,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]

    mova                    m0, [tmpq+ 2*%%str]
    mova                    m3, [tmpq+ 3*%%str]
    VP9_IDCT8_WRITEx2        0,  3,  1,  2,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    mova                    m0, [tmpq+ 7*%%str]
    mova                    m3, [tmpq+ 0*%%str]
    VP9_IDCT8_WRITEx2        0,  3,  1,  2,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    mova                    m0, [tmpq+14*%%str]
    mova                    m3, [tmpq+11*%%str]
    VP9_IDCT8_WRITEx2        0,  3,  1,  2,  4, ROUND_REG, 6
    lea                   dstq, [dstq+strideq*2]
    mova                    m0, [tmpq+13*%%str]
    mova                    m3, [tmpq+ 1*%%str]
    VP9_IDCT8_WRITEx2        0,  3,  1,  2,  4, ROUND_REG, 6
%endif

    SWAP                     0,  4 ; zero
%undef ROUND_REG
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

IADST16_FN idct,  IDCT16,  iadst, IADST16, sse2
IADST16_FN iadst, IADST16, idct,  IDCT16,  sse2
IADST16_FN iadst, IADST16, iadst, IADST16, sse2
IADST16_FN idct,  IDCT16,  iadst, IADST16, ssse3
IADST16_FN iadst, IADST16, idct,  IDCT16,  ssse3
IADST16_FN iadst, IADST16, iadst, IADST16, ssse3
IADST16_FN idct,  IDCT16,  iadst, IADST16, avx
IADST16_FN iadst, IADST16, idct,  IDCT16,  avx
IADST16_FN iadst, IADST16, iadst, IADST16, avx

; in: data in m[0-15] except m0/m4, which are in [blockq+0] and [blockq+128]
; out: m[0-15] except m6, which is in [blockq+192]
; uses blockq as scratch space
%macro VP9_IADST16_YMM_1D 0
    mova          [blockq+ 32], m3
    mova          [blockq+ 64], m7
    mova          [blockq+ 96], m8

    ; first half of round 1
    VP9_UNPACK_MULSUB_2D_4X  9,  6,  0,  3, 13160,  9760    ; m9/x=t7[d], m6/x=t6[d]
    VP9_UNPACK_MULSUB_2D_4X  1, 14,  4,  7,  2404, 16207    ; m1/x=t15[d], m14/x=t14[d]
    VP9_RND_SH_SUMSUB_BA    14,  6,  7,  3,  8, [pd_8192]   ; m14=t6[w], m6=t14[w]
    VP9_RND_SH_SUMSUB_BA     1,  9,  4,  0,  8, [pd_8192]   ; m1=t7[w], m9=t15[w]

    VP9_UNPACK_MULSUB_2D_4X 13,  2,  4,  7, 15893,  3981    ; m13/x=t3[d], m2/x=t2[d]
    VP9_UNPACK_MULSUB_2D_4X  5, 10,  0,  3,  8423, 14053    ; m5/x=t11[d], m10/x=t10[d]
    VP9_RND_SH_SUMSUB_BA    10,  2,  3,  7,  8, [pd_8192]   ; m10=t2[w], m2=t10[w]
    VP9_RND_SH_SUMSUB_BA     5, 13,  0,  4,  8, [pd_8192]   ; m5=t3[w], m13=t11[w]

    ; half of round 2 t8-15
    VP9_UNPACK_MULSUB_2D_4X  2, 13,  4,  7,  9102, 13623    ; m2/x=t11[d], m13/x=t10[d]
    VP9_UNPACK_MULSUB_2D_4X  9,  6,  3,  0, 13623,  9102    ; m9/x=t14[d], m6/x=t15[d]
    VP9_RND_SH_SUMSUB_BA     9, 13,  3,  7,  8, [pd_8192]   ; m9=t10[w], m13=t14[w]
    VP9_RND_SH_SUMSUB_BA     6,  2,  0,  4,  8, [pd_8192]   ; m6=t11[w], m2=t15[w]

    SUMSUB_BA            w, 14, 10,  8                      ; m14=t2, m10=t6
    SUMSUB_BA            w,  1,  5,  8                      ; m1=t3, m5=t7

    mova                    m0, [blockq+  0]
    mova                    m4, [blockq+128]
    mova                    m3, [blockq+ 32]
    mova                    m7, [blockq+ 64]
    mova                    m8, [blockq+ 96]
    mova          [blockq+  0], m1
    mova          [blockq+128], m14
    mova          [blockq+ 32], m6
    mova          [blockq+ 64], m9
    mova          [blockq+ 96], m10

    ; second half of round 1
    VP9_UNPACK_MULSUB_2D_4X 15,  0,  1,  9, 16364,   804    ; m15/x=t1[d], m0/x=t0[d]
    VP9_UNPACK_MULSUB_2D_4X  7,  8, 10,  6, 11003, 12140    ; m7/x=t9[d], m8/x=t8[d]
    VP9_RND_SH_SUMSUB_BA     8,  0,  6,  9, 14, [pd_8192]   ; m8=t0[w], m0=t8[w]
    VP9_RND_SH_SUMSUB_BA     7, 15, 10,  1, 14, [pd_8192]   ; m7=t1[w], m15=t9[w]

    VP9_UNPACK_MULSUB_2D_4X 11,  4, 10,  6, 14811,  7005    ; m11/x=t5[d], m4/x=t4[d]
    VP9_UNPACK_MULSUB_2D_4X  3, 12,  1,  9,  5520, 15426    ; m3/x=t13[d], m12/x=t12[d]
    VP9_RND_SH_SUMSUB_BA    12,  4,  9,  6, 14, [pd_8192]   ; m12=t4[w], m4=t12[w]
    VP9_RND_SH_SUMSUB_BA     3, 11,  1, 10, 14, [pd_8192]   ; m3=t5[w], m11=t13[w]

    ; second half of round 2 t8-15
    VP9_UNPACK_MULSUB_2D_4X  0, 15,  6, 10, 16069,  3196    ; m15/x=t8[d], m0/x=t9[d]
    VP9_UNPACK_MULSUB_2D_4X 11,  4,  9,  1,  3196, 16069    ; m11/x=t12[d], m4/x=t13[d]
    VP9_RND_SH_SUMSUB_BA    11, 15,  9, 10, 14, [pd_8192]   ; m11=t8[w], m15=t12[w]
    VP9_RND_SH_SUMSUB_BA     4,  0,  1,  6, 14, [pd_8192]   ; m4=t9[w], m0=t13[w]

    SUMSUB_BA            w, 12,  8, 14                      ; m12=t0, m8=t4
    SUMSUB_BA            w,  3,  7, 14                      ; m3=t1, m7=t5

    mova                   m10, [blockq+ 96]
    mova          [blockq+ 96], m12

    ; round 3
    VP9_UNPACK_MULSUB_2D_4X 15,  0,  9, 12, 15137,  6270    ; m15/x=t13[d], m0/x=t12[d]
    VP9_UNPACK_MULSUB_2D_4X  2, 13,  1,  6,  6270, 15137    ; m2/x=t14[d], m13/x=t15[d]
    VP9_RND_SH_SUMSUB_BA     2,  0,  1, 12, 14, [pd_8192]   ; m2=out2[w], m0=t14a[w]
    VP9_RND_SH_SUMSUB_BA    13, 15,  6,  9, 14, [pd_8192]
    PSIGNW                 m13, [pw_m1]                     ; m13=out13[w], m15=t15a[w]

    VP9_UNPACK_MULSUB_2D_4X  8,  7, 12,  9, 15137,  6270    ; m8/x=t5[d], m7/x=t4[d]
    VP9_UNPACK_MULSUB_2D_4X  5, 10,  1,  6,  6270, 15137    ; m5/x=t6[d], m10/x=t7[d]
    VP9_RND_SH_SUMSUB_BA     5,  7,  1,  9, 14, [pd_8192]
    PSIGNW                  m5, [pw_m1]                     ; m5=out3[w], m7=t6[w]
    VP9_RND_SH_SUMSUB_BA    10,  8,  6, 12, 14, [pd_8192]   ; m10=out12[w], m8=t7[w]

    mova                    m1, [blockq+  0]
    mova                   m14, [blockq+128]
    mova                    m6, [blockq+ 32]
    mova                    m9, [blockq+ 64]
    mova                   m12, [blockq+ 96]
    mova          [blockq+  0], m10
    mova          [blockq+128], m5

    SUMSUB_BA            w, 14, 12,  5                      ; m14=out0, m12=t2a
    SUMSUB_BA            w,  1,  3,  5
    PSIGNW                  m1, [pw_m1]                     ; m1=out15, m3=t3a

    SUMSUB_BA            w,  9, 11,  5
    PSIGNW                  m9, [pw_m1]                     ; m9=out1, m11=t10
    SUMSUB_BA            w,  6,  4,  5                      ; m6=out14, m4=t11

    VP9_UNPACK_MULSUB_2W_4X  4, 11, 11585, 11585, [pd_8192],  5, 10 ; m4=out9, m11=out6
    mova                    m5, [blockq+128]
    mova          [blockq+192], m11
    PSIGNW                 m15, [pw_m1]
    VP9_UNPACK_MULSUB_2W_4X 15,  0, 11585, 11585, [pd_8192], 10, 11 ; m15=out5, m0=out10

    PSIGNW                  m3, [pw_m1]
    VP9_UNPACK_MULSUB_2W_4X  3, 12, 11585, 11585, [pd_8192], 10, 11 ; m3=out7,m12=out8
    VP9_UNPACK_MULSUB_2W_4X  8,  7, 11585, 11585, [pd_8192], 10, 11 ; m8=out11,m7=out4

    mova                   m10, [blockq+  0]

    SWAP                     0, 14,  6, 11,  8, 12, 10
    SWAP                     1,  9, 15,  4,  7,  3,  5
    SWAP                     5,  9, 15
%endmacro

%if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
%macro IADST16_YMM_FN 4
INIT_YMM avx2
cglobal vp9_%1_%3_16x16_add, 4, 4, 16, dst, stride, block, eob
    mova                m1, [blockq+ 32]
    mova                m2, [blockq+ 64]
    mova                m3, [blockq+ 96]
    mova                m5, [blockq+160]
    mova                m6, [blockq+192]
    mova                m7, [blockq+224]
    mova                m8, [blockq+256]
    mova                m9, [blockq+288]
    mova               m10, [blockq+320]
    mova               m11, [blockq+352]
    mova               m12, [blockq+384]
    mova               m13, [blockq+416]
    mova               m14, [blockq+448]
    mova               m15, [blockq+480]

    VP9_%2_YMM_1D
    TRANSPOSE16x16W      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, \
                         [blockq+192], [blockq+128], 1
    mova      [blockq+  0], m0
    VP9_%4_YMM_1D

    mova      [blockq+224], m7

    ; store
    VP9_IDCT8_WRITEx2    0,  1, 6, 7, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2    2,  3, 6, 7, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2    4,  5, 6, 7, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    mova                m6, [blockq+192]
    mova                m7, [blockq+224]
    VP9_IDCT8_WRITEx2    6,  7, 1, 2, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2    8,  9, 1, 2, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2   10, 11, 1, 2, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2   12, 13, 1, 2, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]
    VP9_IDCT8_WRITEx2   14, 15, 1, 2, unused, [pw_512], 6
    lea               dstq, [dstq+2*strideq]

    ; at the end of the loop, m0 should still be zero
    ; use that to zero out block coefficients
    pxor                m0, m0
    ZERO_BLOCK      blockq, 32, 16, m0
    RET
%endmacro

IADST16_YMM_FN idct,  IDCT16,  iadst, IADST16
IADST16_YMM_FN iadst, IADST16, idct,  IDCT16
IADST16_YMM_FN iadst, IADST16, iadst, IADST16
%endif

;---------------------------------------------------------------------------------------------
; void vp9_idct_idct_32x32_add_<opt>(uint8_t *dst, ptrdiff_t stride, int16_t *block, int eob);
;---------------------------------------------------------------------------------------------

%macro VP9_IDCT32_1D 2-3 32 ; src, pass, nnzc
%if %2 == 1
%assign %%str mmsize
%else
%assign %%str 64
%endif

    ; first do t0-15, this can be done identical to idct16x16
    VP9_IDCT16_1D_START %1, %3/2, 64*2, tmpq, 2*%%str, 1

    ; store everything on stack to make space available for t16-31
    ; we store interleaved with the output of the second half (t16-31)
    ; so we don't need to allocate extra stack space
    mova    [tmpq+ 0*%%str], m0     ; t0
    mova    [tmpq+ 4*%%str], m1     ; t1
    mova    [tmpq+ 8*%%str], m2     ; t2
    mova    [tmpq+12*%%str], m3     ; t3
    mova    [tmpq+16*%%str], m4     ; t4
    mova    [tmpq+20*%%str], m5     ; t5
%if ARCH_X86_64
    mova    [tmpq+22*%%str], m10    ; t10
    mova    [tmpq+18*%%str], m11    ; t11
    mova    [tmpq+14*%%str], m12    ; t12
    mova    [tmpq+10*%%str], m13    ; t13
    mova    [tmpq+ 6*%%str], m14    ; t14
    mova    [tmpq+ 2*%%str], m15    ; t15
%endif

    mova                m0, [tmpq+ 30*%%str]
    UNSCRATCH            1,  6, tmpq+26*%%str
    UNSCRATCH            2,  8, tmpq+24*%%str
    UNSCRATCH            3,  9, tmpq+28*%%str
    SUMSUB_BA            w,  1,  3, 4       ; t6, t9
    SUMSUB_BA            w,  0,  2, 4       ; t7, t8

    mova    [tmpq+24*%%str], m1     ; t6
    mova    [tmpq+28*%%str], m0     ; t7
    mova    [tmpq+30*%%str], m2     ; t8
    mova    [tmpq+26*%%str], m3     ; t9

    ; then, secondly, do t16-31
%if %3 <= 8
    mova                 m4, [%1+ 1*64]
    mova                 m7, [%1+ 7*64]

    pmulhrsw             m1,  m4, [pw_16364x2] ;t31
    pmulhrsw             m4, [pw_804x2] ;t16

    VP9_UNPACK_MULSUB_2W_4X   5,  0,  1,  4, 16069,  3196, [pd_8192], 6,  2 ; t17, t30

    pmulhrsw             m3,  m7, [pw_m5520x2] ;t19
    pmulhrsw             m7, [pw_15426x2] ;t28

    SCRATCH               4, 13, tmpq+ 1*%%str
    SCRATCH               5, 12, tmpq+15*%%str

    VP9_UNPACK_MULSUB_2W_4X   2,  6,  7,  3, 3196, m16069, [pd_8192], 4,  5 ; t18, t29
%else
    mova                 m0, [%1+ 1*64]
    mova                 m1, [%1+15*64]
%if %3 <= 16
    pmulhrsw             m5, m0, [pw_16364x2]
    pmulhrsw             m0, [pw_804x2]
    pmulhrsw             m4, m1, [pw_m11003x2]
    pmulhrsw             m1, [pw_12140x2]
%else
    mova                 m4, [%1+17*64]
    mova                 m5, [%1+31*64]

    VP9_UNPACK_MULSUB_2W_4X   0,  5, 16364,   804, [pd_8192], 2, 3 ; t16, t31
    VP9_UNPACK_MULSUB_2W_4X   4,  1, 11003, 12140, [pd_8192], 2, 3 ; t17, t30
%endif
    SUMSUB_BA             w,  4,  0,  2
    SUMSUB_BA             w,  1,  5,  2

    VP9_UNPACK_MULSUB_2W_4X   5,  0, 16069,  3196, [pd_8192], 2, 3 ; t17, t30

    SCRATCH               4, 13, tmpq+ 1*%%str
    SCRATCH               5, 12, tmpq+15*%%str

    mova                 m2, [%1+ 7*64]
    mova                 m3, [%1+ 9*64]
%if %3 <= 16
    pmulhrsw             m7,  m3, [pw_14811x2]
    pmulhrsw             m3, [pw_7005x2]
    pmulhrsw             m6,  m2, [pw_m5520x2]
    pmulhrsw             m2, [pw_15426x2]
%else
    mova                 m7, [%1+23*64]
    mova                 m6, [%1+25*64]

    VP9_UNPACK_MULSUB_2W_4X   3,  7, 14811,  7005, [pd_8192], 4, 5 ; t18, t29
    VP9_UNPACK_MULSUB_2W_4X   6,  2,  5520, 15426, [pd_8192], 4, 5 ; t19, t28
%endif
    SUMSUB_BA             w,  3,  6,  4
    SUMSUB_BA             w,  7,  2,  4

    VP9_UNPACK_MULSUB_2W_4X   2,  6, 3196, m16069, [pd_8192], 4, 5 ; t18, t29
%endif

    UNSCRATCH             5, 12, tmpq+15*%%str
    SUMSUB_BA             w,  6,  0,  4
    mova    [tmpq+25*%%str], m6             ; t19
    UNSCRATCH             4, 13, tmpq+ 1*%%str
    SUMSUB_BA             w,  7,  1,  6
    SUMSUB_BA             w,  3,  4,  6
    mova    [tmpq+23*%%str], m3             ; t16
    SUMSUB_BA             w,  2,  5,  6

    VP9_UNPACK_MULSUB_2W_4X   0,  5, 15137,  6270, [pd_8192], 6, 3 ; t18, t29
    VP9_UNPACK_MULSUB_2W_4X   1,  4, 15137,  6270, [pd_8192], 6, 3 ; t19, t28

    SCRATCH               0, 10, tmpq+ 1*%%str
    SCRATCH               1, 11, tmpq+ 7*%%str
    SCRATCH               2,  9, tmpq+ 9*%%str
    SCRATCH               4, 14, tmpq+15*%%str
    SCRATCH               5, 15, tmpq+17*%%str
    SCRATCH               7, 13, tmpq+31*%%str

%if %3 <= 8
    mova                 m0, [%1+ 5*64]
    mova                 m3, [%1+ 3*64]

    pmulhrsw             m5,  m0, [pw_15893x2] ;t27
    pmulhrsw             m0, [pw_3981x2] ;t20

    VP9_UNPACK_MULSUB_2W_4X   1,  4,  5,  0,  9102, 13623, [pd_8192], 7,  2 ; t21, t26

    pmulhrsw             m6,  m3, [pw_m2404x2] ;t23
    pmulhrsw             m3, [pw_16207x2] ;t24

    SCRATCH               5,  8, tmpq+ 5*%%str
    SCRATCH               4, 12, tmpq+11*%%str

    VP9_UNPACK_MULSUB_2W_4X   7,  2,  3,  6, 13623, m9102, [pd_8192], 4, 5 ; t22, t25
%else
    mova                 m4, [%1+ 5*64]
    mova                 m5, [%1+11*64]
%if %3 <= 16
    pmulhrsw             m1, m4, [pw_15893x2]
    pmulhrsw             m4, [pw_3981x2]
    pmulhrsw             m0, m5, [pw_m8423x2]
    pmulhrsw             m5, [pw_14053x2]
%else
    mova                 m0, [%1+21*64]
    mova                 m1, [%1+27*64]

    VP9_UNPACK_MULSUB_2W_4X   4,  1, 15893,  3981, [pd_8192], 2, 3 ; t20, t27
    VP9_UNPACK_MULSUB_2W_4X   0,  5,  8423, 14053, [pd_8192], 2, 3 ; t21, t26
%endif
    SUMSUB_BA             w,  0,  4,  2
    SUMSUB_BA             w,  5,  1,  2

    VP9_UNPACK_MULSUB_2W_4X   1,  4,  9102, 13623, [pd_8192], 2, 3 ; t21, t26

    SCRATCH               5,  8, tmpq+ 5*%%str
    SCRATCH               4, 12, tmpq+11*%%str

    mova                 m7, [%1+ 3*64]
    mova                 m6, [%1+13*64]
%if %3 <= 16
    pmulhrsw             m3, m6, [pw_13160x2]
    pmulhrsw             m6, [pw_9760x2]
    pmulhrsw             m2, m7, [pw_m2404x2]
    pmulhrsw             m7, [pw_16207x2]
%else
    mova                 m2, [%1+29*64]
    mova                 m3, [%1+19*64]
    VP9_UNPACK_MULSUB_2W_4X   6,  3, 13160,  9760, [pd_8192], 4, 5 ; t22, t25
    VP9_UNPACK_MULSUB_2W_4X   2,  7,  2404, 16207, [pd_8192], 4, 5 ; t23, t24
%endif
    SUMSUB_BA             w,  6,  2,  4
    SUMSUB_BA             w,  3,  7,  4

    VP9_UNPACK_MULSUB_2W_4X   7,  2, 13623, m9102, [pd_8192], 4, 5 ; t22, t25
%endif

    ; m4=t16, m5=t17, m9=t18, m8=t19, m0=t20, m1=t21, m13=t22, m12=t23,
    ; m3=t24, m2=t25, m14=t26, m15=t27, m7=t28, m6=t29, m10=t30, m11=t31

    UNSCRATCH             4, 12, tmpq+11*%%str
    SUMSUB_BA             w,  0,  6, 5
    SUMSUB_BA             w,  4,  2, 5
    UNSCRATCH             5,  8, tmpq+ 5*%%str
    SCRATCH               4,  8, tmpq+11*%%str
    SUMSUB_BA             w,  1,  7, 4
    SUMSUB_BA             w,  5,  3, 4
    SCRATCH               5, 12, tmpq+ 5*%%str

    VP9_UNPACK_MULSUB_2W_4X   3,  6, 6270, m15137, [pd_8192], 4, 5 ; t20, t27
    VP9_UNPACK_MULSUB_2W_4X   2,  7, 6270, m15137, [pd_8192], 4, 5 ; t21, t26

    ; m8[s]=t16, m9=t17, m5=t18, m4[s]=t19, m12=t20, m13=t21, m1=t22, m0=t23,
    ; m15=t24, m14=t25, m2=t26, m3=t27, m11=t28, m10=t29, m6=t30, m7=t31

    UNSCRATCH             5,  9, tmpq+ 9*%%str
    mova                 m4, [tmpq+23*%%str] ; t16
%if ARCH_X86_64
    SUMSUB_BA             w,  1,  5,  9
    SUMSUB_BA             w,  0,  4,  9
%else
    SUMSUB_BADC           w,  1,  5,  0,  4
%endif
    mova    [tmpq+29*%%str], m1     ; t17
    mova    [tmpq+21*%%str], m0     ; t16
    UNSCRATCH             0, 10, tmpq+ 1*%%str
    UNSCRATCH             1, 11, tmpq+ 7*%%str
%if ARCH_X86_64
    SUMSUB_BA             w,  2,  0,  9
    SUMSUB_BA             w,  3,  1,  9
%else
    SUMSUB_BADC           w,  2,  0,  3,  1
%endif
    mova    [tmpq+ 9*%%str], m2     ; t18
    mova    [tmpq+13*%%str], m3     ; t19
    SCRATCH               0, 10, tmpq+23*%%str
    SCRATCH               1, 11, tmpq+27*%%str

    UNSCRATCH             2, 14, tmpq+15*%%str
    UNSCRATCH             3, 15, tmpq+17*%%str
    SUMSUB_BA             w,  6,  2, 0
    SUMSUB_BA             w,  7,  3, 0
    SCRATCH               6, 14, tmpq+ 3*%%str
    SCRATCH               7, 15, tmpq+ 7*%%str

    UNSCRATCH             0,  8, tmpq+11*%%str
    mova                 m1, [tmpq+25*%%str] ; t19
    UNSCRATCH             6, 12, tmpq+ 5*%%str
    UNSCRATCH             7, 13, tmpq+31*%%str
%if ARCH_X86_64
    SUMSUB_BA             w,  0,  1,  9
    SUMSUB_BA             w,  6,  7,  9
%else
    SUMSUB_BADC           w,  0,  1,  6,  7
%endif

    ; m0=t16, m1=t17, m2=t18, m3=t19, m11=t20, m10=t21, m9=t22, m8=t23,
    ; m7=t24, m6=t25, m5=t26, m4=t27, m12=t28, m13=t29, m14=t30, m15=t31

%if 0; cpuflag(ssse3)
%if ARCH_X86_64
    SUMSUB_BA             w,  4,  7,  8
    SUMSUB_BA             w,  5,  1,  8
%else
    SUMSUB_BADC           w,  4,  7,  5,  1
%endif

    pmulhrsw             m7, [pw_11585x2]
    pmulhrsw             m4, [pw_11585x2]
    pmulhrsw             m1, [pw_11585x2]
    pmulhrsw             m5, [pw_11585x2]

    mova    [tmpq+ 5*%%str], m7     ; t23
    SCRATCH               1, 13, tmpq+25*%%str
    UNSCRATCH             7, 10, tmpq+23*%%str
    UNSCRATCH             1, 11, tmpq+27*%%str

%if ARCH_X86_64
    SUMSUB_BA             w,  7,  3, 10
    SUMSUB_BA             w,  1,  2, 10
%else
    SUMSUB_BADC           w,  7,  3,  1,  2
%endif

    pmulhrsw             m3, [pw_11585x2]
    pmulhrsw             m7, [pw_11585x2]
    pmulhrsw             m2, [pw_11585x2]
    pmulhrsw             m1, [pw_11585x2]
%else
    SCRATCH               0,  8, tmpq+15*%%str
    SCRATCH               6,  9, tmpq+17*%%str
    VP9_UNPACK_MULSUB_2W_4X  7,  4, 11585, 11585, [pd_8192], 0, 6
    mova    [tmpq+ 5*%%str], m7     ; t23
    UNSCRATCH             7, 10, tmpq+23*%%str
    VP9_UNPACK_MULSUB_2W_4X  1,  5, 11585, 11585, [pd_8192], 0, 6
    SCRATCH               1, 13, tmpq+25*%%str
    UNSCRATCH             1, 11, tmpq+27*%%str
    VP9_UNPACK_MULSUB_2W_4X  3,  7, 11585, 11585, [pd_8192], 0, 6
    VP9_UNPACK_MULSUB_2W_4X  2,  1, 11585, 11585, [pd_8192], 0, 6
    UNSCRATCH             0,  8, tmpq+15*%%str
    UNSCRATCH             6,  9, tmpq+17*%%str
%endif

    ; m0=t16, m1=t17, m2=t18, m3=t19, m4=t20, m5=t21, m6=t22, m7=t23,
    ; m8=t24, m9=t25, m10=t26, m11=t27, m12=t28, m13=t29, m14=t30, m15=t31

    ; then do final pass to sumsub+store the two halves
%if %2 == 1
    mova    [tmpq+17*%%str], m2     ; t20
    mova    [tmpq+ 1*%%str], m3     ; t21
%if ARCH_X86_64
    mova    [tmpq+25*%%str], m13    ; t22

    mova                 m8, [tmpq+ 0*%%str] ; t0
    mova                 m9, [tmpq+ 4*%%str] ; t1
    mova                m12, [tmpq+ 8*%%str] ; t2
    mova                m11, [tmpq+12*%%str] ; t3
    mova                 m2, [tmpq+16*%%str] ; t4
    mova                 m3, [tmpq+20*%%str] ; t5
    mova                m13, [tmpq+24*%%str] ; t6

    SUMSUB_BA             w,  6,  8, 10
    mova    [tmpq+ 3*%%str], m8              ; t15
    SUMSUB_BA             w,  0,  9,  8
    SUMSUB_BA             w, 15, 12,  8
    SUMSUB_BA             w, 14, 11,  8
    SUMSUB_BA             w,  1,  2,  8
    SUMSUB_BA             w,  7,  3,  8
    SUMSUB_BA             w,  5, 13,  8
    mova                m10, [tmpq+28*%%str] ; t7
    SUMSUB_BA             w,  4, 10,  8
%if cpuflag(avx2)
    ; the "shitty" about this idct is that the final pass does the outermost
    ; interleave sumsubs (t0/31, t1/30, etc) but the tN for the 16x16 need
    ; to be sequential, which means I need to load/store half of the sumsub
    ; intermediates back to/from memory to get a 16x16 transpose going...
    ; This would be easier if we had more (e.g. 32) YMM regs here.
    mova    [tmpq+ 7*%%str], m9
    mova    [tmpq+11*%%str], m12
    mova    [tmpq+15*%%str], m11
    mova    [tmpq+19*%%str], m2
    mova    [tmpq+23*%%str], m3
    mova    [tmpq+27*%%str], m13
    mova    [tmpq+31*%%str], m10
    mova    [tmpq+12*%%str], m5

    mova                m13, [tmpq+30*%%str] ; t8
    mova                m12, [tmpq+26*%%str] ; t9
    mova                m11, [tmpq+22*%%str] ; t10
    mova                m10, [tmpq+18*%%str] ; t11
    mova                 m9, [tmpq+17*%%str] ; t20
    mova                 m8, [tmpq+ 1*%%str] ; t21
    mova                 m3, [tmpq+25*%%str] ; t22
    mova                 m2, [tmpq+ 5*%%str] ; t23

    SUMSUB_BA             w,  9, 10, 5
    SUMSUB_BA             w,  8, 11, 5
    SUMSUB_BA             w,  3, 12, 5
    SUMSUB_BA             w,  2, 13, 5
    mova    [tmpq+ 1*%%str], m10
    mova    [tmpq+ 5*%%str], m11
    mova    [tmpq+17*%%str], m12
    mova    [tmpq+25*%%str], m13

    mova                m13, [tmpq+14*%%str] ; t12
    mova                m12, [tmpq+10*%%str] ; t13
    mova                m11, [tmpq+ 9*%%str] ; t18
    mova                m10, [tmpq+13*%%str] ; t19

    SUMSUB_BA             w, 11, 12, 5
    SUMSUB_BA             w, 10, 13, 5
    mova    [tmpq+ 9*%%str], m13
    mova    [tmpq+13*%%str], m12
    mova    [tmpq+10*%%str], m10
    mova    [tmpq+14*%%str], m11

    mova                m13, [tmpq+ 6*%%str] ; t14
    mova                m12, [tmpq+ 2*%%str] ; t15
    mova                m11, [tmpq+21*%%str] ; t16
    mova                m10, [tmpq+29*%%str] ; t17
    SUMSUB_BA             w, 11, 12, 5
    SUMSUB_BA             w, 10, 13, 5
    mova    [tmpq+21*%%str], m12
    mova    [tmpq+29*%%str], m13
    mova                m12, [tmpq+10*%%str]
    mova                m13, [tmpq+14*%%str]

    TRANSPOSE16x16W       6,  0, 15, 14,  1,  7,  5,  4, \
                          2,  3,  8,  9, 12, 13, 10, 11, \
            [tmpq+12*%%str], [tmpq+ 8*%%str], 1
    mova    [tmpq+ 0*%%str], m6
    mova    [tmpq+ 2*%%str], m0
    mova    [tmpq+ 4*%%str], m15
    mova    [tmpq+ 6*%%str], m14
    mova    [tmpq+10*%%str], m7
    mova    [tmpq+12*%%str], m5
    mova    [tmpq+14*%%str], m4
    mova    [tmpq+16*%%str], m2
    mova    [tmpq+18*%%str], m3
    mova    [tmpq+20*%%str], m8
    mova    [tmpq+22*%%str], m9
    mova    [tmpq+24*%%str], m12
    mova    [tmpq+26*%%str], m13
    mova    [tmpq+28*%%str], m10
    mova    [tmpq+30*%%str], m11

    mova                 m0, [tmpq+21*%%str]
    mova                 m1, [tmpq+29*%%str]
    mova                 m2, [tmpq+13*%%str]
    mova                 m3, [tmpq+ 9*%%str]
    mova                 m4, [tmpq+ 1*%%str]
    mova                 m5, [tmpq+ 5*%%str]
    mova                 m7, [tmpq+25*%%str]
    mova                 m8, [tmpq+31*%%str]
    mova                 m9, [tmpq+27*%%str]
    mova                m10, [tmpq+23*%%str]
    mova                m11, [tmpq+19*%%str]
    mova                m12, [tmpq+15*%%str]
    mova                m13, [tmpq+11*%%str]
    mova                m14, [tmpq+ 7*%%str]
    mova                m15, [tmpq+ 3*%%str]
    TRANSPOSE16x16W       0,  1,  2,  3,  4,  5,  6,  7, \
                          8,  9, 10, 11, 12, 13, 14, 15, \
            [tmpq+17*%%str], [tmpq+ 9*%%str], 1
    mova    [tmpq+ 1*%%str], m0
    mova    [tmpq+ 3*%%str], m1
    mova    [tmpq+ 5*%%str], m2
    mova    [tmpq+ 7*%%str], m3
    mova    [tmpq+11*%%str], m5
    mova    [tmpq+13*%%str], m6
    mova    [tmpq+15*%%str], m7
    mova    [tmpq+17*%%str], m8
    mova    [tmpq+19*%%str], m9
    mova    [tmpq+21*%%str], m10
    mova    [tmpq+23*%%str], m11
    mova    [tmpq+25*%%str], m12
    mova    [tmpq+27*%%str], m13
    mova    [tmpq+29*%%str], m14
    mova    [tmpq+31*%%str], m15
%else ; !avx2
    TRANSPOSE8x8W         6, 0, 15, 14, 1, 7, 5, 4, 8
    mova    [tmpq+ 0*%%str], m6
    mova    [tmpq+ 4*%%str], m0
    mova    [tmpq+ 8*%%str], m15
    mova    [tmpq+12*%%str], m14
    mova    [tmpq+16*%%str], m1
    mova    [tmpq+20*%%str], m7
    mova    [tmpq+24*%%str], m5
    mova    [tmpq+28*%%str], m4

    mova                  m8, [tmpq+ 3*%%str] ; t15
    TRANSPOSE8x8W         10, 13, 3, 2, 11, 12, 9, 8, 0
    mova    [tmpq+ 3*%%str], m10
    mova    [tmpq+ 7*%%str], m13
    mova    [tmpq+11*%%str], m3
    mova    [tmpq+15*%%str], m2
    mova    [tmpq+19*%%str], m11
    mova    [tmpq+23*%%str], m12
    mova    [tmpq+27*%%str], m9
    mova    [tmpq+31*%%str], m8

    mova                m15, [tmpq+30*%%str] ; t8
    mova                m14, [tmpq+26*%%str] ; t9
    mova                m13, [tmpq+22*%%str] ; t10
    mova                m12, [tmpq+18*%%str] ; t11
    mova                m11, [tmpq+14*%%str] ; t12
    mova                m10, [tmpq+10*%%str] ; t13
    mova                 m9, [tmpq+ 6*%%str] ; t14
    mova                 m8, [tmpq+ 2*%%str] ; t15
    mova                 m7, [tmpq+21*%%str] ; t16
    mova                 m6, [tmpq+29*%%str] ; t17
    mova                 m5, [tmpq+ 9*%%str] ; t18
    mova                 m4, [tmpq+13*%%str] ; t19
    mova                 m3, [tmpq+17*%%str] ; t20
    mova                 m2, [tmpq+ 1*%%str] ; t21
    mova                 m1, [tmpq+25*%%str] ; t22

    SUMSUB_BA             w,  7,  8, 0
    mova    [tmpq+ 2*%%str], m8
    mova                 m0, [tmpq+ 5*%%str] ; t23
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
%endif ; avx2
%else
    mova                 m2, [tmpq+24*%%str] ; t6
    mova                 m3, [tmpq+28*%%str] ; t7
    SUMSUB_BADC           w,  5,  2,  4,  3
    mova    [tmpq+24*%%str], m5
    mova    [tmpq+23*%%str], m2
    mova    [tmpq+28*%%str], m4
    mova    [tmpq+19*%%str], m3

    mova                 m2, [tmpq+16*%%str] ; t4
    mova                 m3, [tmpq+20*%%str] ; t5
    SUMSUB_BA             w,  1,  2,  5
    SUMSUB_BA             w,  7,  3,  5
    mova    [tmpq+15*%%str], m2
    mova    [tmpq+11*%%str], m3

    mova                 m2, [tmpq+ 0*%%str] ; t0
    mova                 m3, [tmpq+ 4*%%str] ; t1
    SUMSUB_BA             w,  6,  2,  5
    SUMSUB_BA             w,  0,  3,  5
    mova    [tmpq+31*%%str], m2
    mova    [tmpq+27*%%str], m3

    mova                 m2, [tmpq+ 8*%%str] ; t2
    mova                 m3, [tmpq+12*%%str] ; t3
    mova                 m5, [tmpq+ 7*%%str]
    mova                 m4, [tmpq+ 3*%%str]
    SUMSUB_BADC           w,  5,  2,  4,  3
    mova    [tmpq+ 7*%%str], m2
    mova    [tmpq+ 3*%%str], m3

    mova                 m3, [tmpq+28*%%str]
    TRANSPOSE8x8W         6, 0, 5, 4, 1, 7, 2, 3, [tmpq+24*%%str], [tmpq+16*%%str], 1
    mova    [tmpq+ 0*%%str], m6
    mova    [tmpq+ 4*%%str], m0
    mova    [tmpq+ 8*%%str], m5
    mova    [tmpq+12*%%str], m4
    mova    [tmpq+20*%%str], m7
    mova    [tmpq+24*%%str], m2
    mova    [tmpq+28*%%str], m3

    mova                 m6, [tmpq+19*%%str]
    mova                 m0, [tmpq+23*%%str]
    mova                 m5, [tmpq+11*%%str]
    mova                 m4, [tmpq+15*%%str]
    mova                 m1, [tmpq+ 3*%%str]
    mova                 m7, [tmpq+ 7*%%str]
    mova                 m3, [tmpq+31*%%str]
    TRANSPOSE8x8W         6, 0, 5, 4, 1, 7, 2, 3, [tmpq+27*%%str], [tmpq+19*%%str], 1
    mova    [tmpq+ 3*%%str], m6
    mova    [tmpq+ 7*%%str], m0
    mova    [tmpq+11*%%str], m5
    mova    [tmpq+15*%%str], m4
    mova    [tmpq+23*%%str], m7
    mova    [tmpq+27*%%str], m2
    mova    [tmpq+31*%%str], m3

    mova                 m1, [tmpq+ 6*%%str] ; t14
    mova                 m0, [tmpq+ 2*%%str] ; t15
    mova                 m7, [tmpq+21*%%str] ; t16
    mova                 m6, [tmpq+29*%%str] ; t17
    SUMSUB_BA             w,  7,  0,  2
    SUMSUB_BA             w,  6,  1,  2
    mova    [tmpq+29*%%str], m7
    mova    [tmpq+ 2*%%str], m0
    mova    [tmpq+21*%%str], m6
    mova    [tmpq+ 6*%%str], m1

    mova                 m1, [tmpq+14*%%str] ; t12
    mova                 m0, [tmpq+10*%%str] ; t13
    mova                 m5, [tmpq+ 9*%%str] ; t18
    mova                 m4, [tmpq+13*%%str] ; t19
    SUMSUB_BA             w,  5,  0,  2
    SUMSUB_BA             w,  4,  1,  2
    mova     [tmpq+10*%%str], m0
    mova     [tmpq+14*%%str], m1

    mova                 m1, [tmpq+22*%%str] ; t10
    mova                 m0, [tmpq+18*%%str] ; t11
    mova                 m3, [tmpq+17*%%str] ; t20
    mova                 m2, [tmpq+ 1*%%str] ; t21
    SUMSUB_BA             w,  3,  0,  6
    SUMSUB_BA             w,  2,  1,  6
    mova     [tmpq+18*%%str], m0
    mova     [tmpq+22*%%str], m1

    mova                 m7, [tmpq+30*%%str] ; t8
    mova                 m6, [tmpq+26*%%str] ; t9
    mova                 m1, [tmpq+25*%%str] ; t22
    mova                 m0, [tmpq+ 5*%%str] ; t23
    SUMSUB_BADC           w,  1,  6,  0,  7
    mova     [tmpq+26*%%str], m6
    mova     [tmpq+30*%%str], m7

    mova                 m7, [tmpq+29*%%str]
    TRANSPOSE8x8W         0, 1, 2, 3, 4, 5, 6, 7, [tmpq+21*%%str], [tmpq+17*%%str], 1
    mova    [tmpq+ 1*%%str], m0
    mova    [tmpq+ 5*%%str], m1
    mova    [tmpq+ 9*%%str], m2
    mova    [tmpq+13*%%str], m3
    mova    [tmpq+21*%%str], m5
    mova    [tmpq+25*%%str], m6
    mova    [tmpq+29*%%str], m7

    mova                 m0, [tmpq+ 2*%%str]
    mova                 m1, [tmpq+ 6*%%str]
    mova                 m2, [tmpq+10*%%str]
    mova                 m3, [tmpq+14*%%str]
    mova                 m4, [tmpq+18*%%str]
    mova                 m5, [tmpq+22*%%str]
    mova                 m7, [tmpq+30*%%str]
    TRANSPOSE8x8W         0, 1, 2, 3, 4, 5, 6, 7, [tmpq+26*%%str], [tmpq+18*%%str], 1
    mova    [tmpq+ 2*%%str], m0
    mova    [tmpq+ 6*%%str], m1
    mova    [tmpq+10*%%str], m2
    mova    [tmpq+14*%%str], m3
    mova    [tmpq+22*%%str], m5
    mova    [tmpq+26*%%str], m6
    mova    [tmpq+30*%%str], m7
%endif
%else
    ; t0-7 is in [tmpq+{0,4,8,12,16,20,24,28}*%%str]
    ; t8-15 is in [tmpq+{2,6,10,14,18,22,26,30}*%%str]
    ; t16-19 and t23 is in [tmpq+{1,5,9,13,29}*%%str]
    ; t20-22 is in m4-6
    ; t24-31 is in m8-15

%if cpuflag(ssse3)
%define ROUND_REG [pw_512]
%else
%define ROUND_REG [pw_32]
%endif

%macro %%STORE_2X2 7-8 1 ; src[1-4], tmp[1-2], zero, inc_dst_ptrs
    SUMSUB_BA            w, %4, %1, %5
    SUMSUB_BA            w, %3, %2, %5
    VP9_IDCT8_WRITEx2   %4, %3, %5, %6, %7, ROUND_REG, 6
%if %8 == 1
    add               dstq, stride2q
%endif
    VP9_IDCT8_WRITEx2   %2, %1, %5, %6, %7, ROUND_REG, 6, dst_endq
%if %8 == 1
    sub           dst_endq, stride2q
%endif
%endmacro

%if ARCH_X86_64
    pxor               m10, m10

    ; store t0-1 and t30-31
    mova                m8, [tmpq+ 0*%%str]
    mova                m9, [tmpq+ 4*%%str]
    %%STORE_2X2          8,  9,  0,  6, 12, 11, 10

    ; store t2-3 and t28-29
    mova                m8, [tmpq+ 8*%%str]
    mova                m9, [tmpq+12*%%str]
    %%STORE_2X2          8,  9, 14, 15, 12, 11, 10

    ; store t4-5 and t26-27
    mova                m8, [tmpq+16*%%str]
    mova                m9, [tmpq+20*%%str]
    %%STORE_2X2          8,  9,  7,  1, 12, 11, 10

    ; store t6-7 and t24-25
    mova                m8, [tmpq+24*%%str]
    mova                m9, [tmpq+28*%%str]
    %%STORE_2X2          8,  9,  4,  5, 12, 11, 10

    ; store t8-9 and t22-23
    mova                m8, [tmpq+30*%%str]
    mova                m9, [tmpq+26*%%str]
    mova                m0, [tmpq+ 5*%%str]
    %%STORE_2X2          8,  9, 13,  0, 12, 11, 10

    ; store t10-11 and t20-21
    mova                m8, [tmpq+22*%%str]
    mova                m9, [tmpq+18*%%str]
    %%STORE_2X2          8,  9,  2,  3, 12, 11, 10

    ; store t12-13 and t18-19
    mova                m8, [tmpq+14*%%str]
    mova                m9, [tmpq+10*%%str]
    mova                m5, [tmpq+13*%%str]
    mova                m4, [tmpq+ 9*%%str]
    %%STORE_2X2          8,  9,  4,  5, 12, 11, 10

    ; store t14-17
    mova                m8, [tmpq+ 6*%%str]
    mova                m9, [tmpq+ 2*%%str]
    mova                m5, [tmpq+29*%%str]
    mova                m4, [tmpq+21*%%str]
    %%STORE_2X2          8,  9,  4,  5, 12, 11, 10, 0

    SWAP                 1, 10 ; zero
%else
    mova   [tmpq+ 1*%%str], m1
    mova   [tmpq+11*%%str], m2
    mova   [tmpq+15*%%str], m3
    mova   [tmpq+17*%%str], m4
    mova   [tmpq+19*%%str], m5
    pxor                m1, m1

    ; store t0-1 and t30-31
    mova                m2, [tmpq+ 0*%%str]
    mova                m3, [tmpq+ 4*%%str]
    %%STORE_2X2          2,  3,  0,  6, 4, 5, 1

    ; store t2-3 and t28-29
    mova                m2, [tmpq+ 8*%%str]
    mova                m3, [tmpq+12*%%str]
    mova                m0, [tmpq+ 3*%%str]
    mova                m6, [tmpq+ 7*%%str]
    %%STORE_2X2          2,  3,  0,  6, 4, 5, 1

    ; store t4-5 and t26-27
    mova                m2, [tmpq+16*%%str]
    mova                m3, [tmpq+20*%%str]
    mova                m0, [tmpq+ 1*%%str]
    %%STORE_2X2          2,  3,  7,  0, 4, 5, 1

    ; store t6-7 and t24-25
    mova                m2, [tmpq+24*%%str]
    mova                m3, [tmpq+28*%%str]
    mova                m0, [tmpq+17*%%str]
    mova                m6, [tmpq+19*%%str]
    %%STORE_2X2          2,  3,  0,  6, 4, 5, 1

    ; store t8-9 and t22-23
    mova                m2, [tmpq+30*%%str]
    mova                m3, [tmpq+26*%%str]
    mova                m0, [tmpq+25*%%str]
    mova                m6, [tmpq+ 5*%%str]
    %%STORE_2X2          2,  3,  0,  6, 4, 5, 1

    ; store t10-11 and t20-21
    mova                m2, [tmpq+22*%%str]
    mova                m3, [tmpq+18*%%str]
    mova                m0, [tmpq+11*%%str]
    mova                m6, [tmpq+15*%%str]
    %%STORE_2X2          2,  3,  0,  6, 4, 5, 1

    ; store t12-13 and t18-19
    mova                m2, [tmpq+14*%%str]
    mova                m3, [tmpq+10*%%str]
    mova                m6, [tmpq+13*%%str]
    mova                m0, [tmpq+ 9*%%str]
    %%STORE_2X2          2,  3,  0,  6, 4, 5, 1

    ; store t14-17
    mova                m2, [tmpq+ 6*%%str]
    mova                m3, [tmpq+ 2*%%str]
    mova                m6, [tmpq+29*%%str]
    mova                m0, [tmpq+21*%%str]
    %%STORE_2X2          2,  3,  0,  6, 4, 5, 1, 0
%endif
%undef ROUND_REG
%endif
%endmacro

%macro VP9_IDCT_IDCT_32x32_ADD_XMM 1
INIT_XMM %1
cglobal vp9_idct_idct_32x32_add, 0, 6 + ARCH_X86_64 * 3, 16, 2048, dst, stride, block, eob
    movifnidn         eobd, dword eobm
%if cpuflag(ssse3)
    cmp eobd, 135
    jg .idctfull
    cmp eobd, 34
    jg .idct16x16
    cmp eobd, 1
    jg .idct8x8
%else
    cmp eobd, 1
    jg .idctfull
%endif

    ; dc-only case
    movifnidn       blockq, blockmp
    movifnidn         dstq, dstmp
    movifnidn      strideq, stridemp
%if cpuflag(ssse3)
    movd                m0, [blockq]
    mova                m1, [pw_11585x2]
    pmulhrsw            m0, m1
    pmulhrsw            m0, m1
%else
    DEFINE_ARGS dst, stride, block, coef
    movsx            coefd, word [blockq]
    imul             coefd, 11585
    add              coefd, 8192
    sar              coefd, 14
    imul             coefd, 11585
    add              coefd, (32 << 14) + 8192
    sar              coefd, 14 + 6
    movd                m0, coefd
%endif
    SPLATW              m0, m0, q0000
%if cpuflag(ssse3)
    pmulhrsw            m0, [pw_512]
%endif
    pxor                m5, m5
    movd          [blockq], m5
%rep 31
    VP9_STORE_2XFULL    0, 1, 2, 3, 4, 5, mmsize
    add               dstq, strideq
%endrep
    VP9_STORE_2XFULL    0, 1, 2, 3, 4, 5, mmsize
    RET

%if ARCH_X86_64
    DEFINE_ARGS dst_bak, stride, block, cnt, dst, stride30, dst_end, stride2, tmp
%else
%define dst_bakq r0mp
%endif
%if cpuflag(ssse3)
.idct8x8:
%if ARCH_X86_32
    DEFINE_ARGS block, u1, u2, u3, u4, tmp
    mov             blockq, r2mp
%endif
    mov               tmpq, rsp
    VP9_IDCT32_1D   blockq, 1, 8

%if ARCH_X86_32
    DEFINE_ARGS dst, stride, stride30, dst_end, stride2, tmp
    mov            strideq, r1mp
%define cntd dword r3m
%endif
    mov          stride30q, strideq         ; stride
    lea           stride2q, [strideq*2]     ; stride*2
    shl          stride30q, 5               ; stride*32
    mov               cntd, 4
    sub          stride30q, stride2q        ; stride*30
.loop2_8x8:
    mov               dstq, dst_bakq
    lea           dst_endq, [dstq+stride30q]
    VP9_IDCT32_1D     tmpq, 2, 8
    add           dst_bakq, 8
    add               tmpq, 16
    dec               cntd
    jg .loop2_8x8

    ; at the end of the loop, m7 should still be zero
    ; use that to zero out block coefficients
%if ARCH_X86_32
    DEFINE_ARGS block
    mov             blockq, r2mp
%endif
    ZERO_BLOCK      blockq, 64,  8, m1
    RET

.idct16x16:
%if ARCH_X86_32
    DEFINE_ARGS block, tmp, cnt
    mov             blockq, r2mp
%endif
    mov               cntd, 2
    mov               tmpq, rsp
.loop1_16x16:
    VP9_IDCT32_1D   blockq, 1, 16
    add             blockq, 16
    add               tmpq, 512
    dec               cntd
    jg .loop1_16x16

%if ARCH_X86_64
    sub             blockq, 32
%else
    DEFINE_ARGS dst, stride, stride30, dst_end, stride2, tmp
    mov            strideq, r1mp
%define cntd dword r3m
%endif

    mov          stride30q, strideq         ; stride
    lea           stride2q, [strideq*2]     ; stride*2
    shl          stride30q, 5               ; stride*32
    mov               cntd, 4
    mov               tmpq, rsp
    sub          stride30q, stride2q        ; stride*30
.loop2_16x16:
    mov               dstq, dst_bakq
    lea           dst_endq, [dstq+stride30q]
    VP9_IDCT32_1D     tmpq, 2, 16
    add           dst_bakq, 8
    add               tmpq, 16
    dec               cntd
    jg .loop2_16x16

    ; at the end of the loop, m7 should still be zero
    ; use that to zero out block coefficients
%if ARCH_X86_32
    DEFINE_ARGS block
    mov             blockq, r2mp
%endif
    ZERO_BLOCK      blockq, 64, 16, m1
    RET
%endif

.idctfull:
%if ARCH_X86_32
    DEFINE_ARGS block, tmp, cnt
    mov             blockq, r2mp
%endif
    mov               cntd, 4
    mov               tmpq, rsp
.loop1_full:
    VP9_IDCT32_1D   blockq, 1
    add             blockq, 16
    add               tmpq, 512
    dec               cntd
    jg .loop1_full

%if ARCH_X86_64
    sub             blockq, 64
%else
    DEFINE_ARGS dst, stride, stride30, dst_end, stride2, tmp
    mov            strideq, r1mp
%define cntd dword r3m
%endif

    mov          stride30q, strideq         ; stride
    lea           stride2q, [strideq*2]     ; stride*2
    shl          stride30q, 5               ; stride*32
    mov               cntd, 4
    mov               tmpq, rsp
    sub          stride30q, stride2q        ; stride*30
.loop2_full:
    mov               dstq, dst_bakq
    lea           dst_endq, [dstq+stride30q]
    VP9_IDCT32_1D     tmpq, 2
    add           dst_bakq, 8
    add               tmpq, 16
    dec               cntd
    jg .loop2_full

    ; at the end of the loop, m7 should still be zero
    ; use that to zero out block coefficients
%if ARCH_X86_32
    DEFINE_ARGS block
    mov             blockq, r2mp
%endif
    ZERO_BLOCK      blockq, 64, 32, m1
    RET
%endmacro

VP9_IDCT_IDCT_32x32_ADD_XMM sse2
VP9_IDCT_IDCT_32x32_ADD_XMM ssse3
VP9_IDCT_IDCT_32x32_ADD_XMM avx

; this is almost identical to VP9_STORE_2X, but it does two rows
; for slightly improved interleaving, and it omits vpermq since the
; input is DC so all values are identical
%macro VP9_STORE_YMM_DC_2X2 6 ; reg, tmp1, tmp2, tmp3, tmp4, zero
    mova               m%2, [dstq]
    mova               m%4, [dstq+strideq]
    punpckhbw          m%3, m%2, m%6
    punpcklbw          m%2, m%6
    punpckhbw          m%5, m%4, m%6
    punpcklbw          m%4, m%6
    paddw              m%3, m%1
    paddw              m%2, m%1
    paddw              m%5, m%1
    paddw              m%4, m%1
    packuswb           m%2, m%3
    packuswb           m%4, m%5
    mova  [dstq+strideq*0], m%2
    mova  [dstq+strideq*1], m%4
%endmacro

%if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
INIT_YMM avx2
cglobal vp9_idct_idct_32x32_add, 4, 9, 16, 2048, dst, stride, block, eob
    cmp eobd, 135
    jg .idctfull
    cmp eobd, 1
    jg .idct16x16

    ; dc-only case
    mova                m1, [pw_11585x2]
    vpbroadcastw        m0, [blockq]
    pmulhrsw            m0, m1
    pmulhrsw            m0, m1
    pxor                m5, m5
    pmulhrsw            m0, [pw_512]
    movd          [blockq], xm5

    DEFINE_ARGS dst, stride, cnt
    mov               cntd, 16
.loop_dc:
    VP9_STORE_YMM_DC_2X2 0, 1, 2, 3, 4, 5
    lea               dstq, [dstq+2*strideq]
    dec               cntd
    jg .loop_dc
    RET

    DEFINE_ARGS dst_bak, stride, block, cnt, dst, stride30, dst_end, stride2, tmp
.idct16x16:
    mov               tmpq, rsp
    VP9_IDCT32_1D   blockq, 1, 16

    mov          stride30q, strideq         ; stride
    lea           stride2q, [strideq*2]     ; stride*2
    shl          stride30q, 5               ; stride*32
    mov               cntd, 2
    sub          stride30q, stride2q        ; stride*30
.loop2_16x16:
    mov               dstq, dst_bakq
    lea           dst_endq, [dstq+stride30q]
    VP9_IDCT32_1D     tmpq, 2, 16
    add           dst_bakq, 16
    add               tmpq, 32
    dec               cntd
    jg .loop2_16x16

    ; at the end of the loop, m1 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 64, 16, m1
    RET

.idctfull:
    mov               cntd, 2
    mov               tmpq, rsp
.loop1_full:
    VP9_IDCT32_1D   blockq, 1
    add             blockq, 32
    add               tmpq, 1024
    dec               cntd
    jg .loop1_full

    sub             blockq, 64

    mov          stride30q, strideq         ; stride
    lea           stride2q, [strideq*2]     ; stride*2
    shl          stride30q, 5               ; stride*32
    mov               cntd, 2
    mov               tmpq, rsp
    sub          stride30q, stride2q        ; stride*30
.loop2_full:
    mov               dstq, dst_bakq
    lea           dst_endq, [dstq+stride30q]
    VP9_IDCT32_1D     tmpq, 2
    add           dst_bakq, 16
    add               tmpq, 32
    dec               cntd
    jg .loop2_full

    ; at the end of the loop, m1 should still be zero
    ; use that to zero out block coefficients
    ZERO_BLOCK      blockq, 64, 32, m1
    RET
%endif
