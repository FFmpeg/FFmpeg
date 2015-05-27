;******************************************************************************
;* x86 optimized channel mixing
;* Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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
%include "util.asm"

SECTION .text

;-----------------------------------------------------------------------------
; void ff_mix_2_to_1_fltp_flt(float **src, float **matrix, int len,
;                             int out_ch, int in_ch);
;-----------------------------------------------------------------------------

%macro MIX_2_TO_1_FLTP_FLT 0
cglobal mix_2_to_1_fltp_flt, 3,4,6, src, matrix, len, src1
    mov       src1q, [srcq+gprsize]
    mov        srcq, [srcq        ]
    sub       src1q, srcq
    mov     matrixq, [matrixq  ]
    VBROADCASTSS m4, [matrixq  ]
    VBROADCASTSS m5, [matrixq+4]
    ALIGN 16
.loop:
    mulps        m0, m4, [srcq             ]
    mulps        m1, m5, [srcq+src1q       ]
    mulps        m2, m4, [srcq+      mmsize]
    mulps        m3, m5, [srcq+src1q+mmsize]
    addps        m0, m0, m1
    addps        m2, m2, m3
    mova  [srcq       ], m0
    mova  [srcq+mmsize], m2
    add        srcq, mmsize*2
    sub        lend, mmsize*2/4
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse
MIX_2_TO_1_FLTP_FLT
%if HAVE_AVX_EXTERNAL
INIT_YMM avx
MIX_2_TO_1_FLTP_FLT
%endif

;-----------------------------------------------------------------------------
; void ff_mix_2_to_1_s16p_flt(int16_t **src, float **matrix, int len,
;                             int out_ch, int in_ch);
;-----------------------------------------------------------------------------

%macro MIX_2_TO_1_S16P_FLT 0
cglobal mix_2_to_1_s16p_flt, 3,4,6, src, matrix, len, src1
    mov       src1q, [srcq+gprsize]
    mov        srcq, [srcq]
    sub       src1q, srcq
    mov     matrixq, [matrixq  ]
    VBROADCASTSS m4, [matrixq  ]
    VBROADCASTSS m5, [matrixq+4]
    ALIGN 16
.loop:
    mova         m0, [srcq      ]
    mova         m2, [srcq+src1q]
    S16_TO_S32_SX 0, 1
    S16_TO_S32_SX 2, 3
    cvtdq2ps     m0, m0
    cvtdq2ps     m1, m1
    cvtdq2ps     m2, m2
    cvtdq2ps     m3, m3
    mulps        m0, m4
    mulps        m1, m4
    mulps        m2, m5
    mulps        m3, m5
    addps        m0, m2
    addps        m1, m3
    cvtps2dq     m0, m0
    cvtps2dq     m1, m1
    packssdw     m0, m1
    mova     [srcq], m0
    add        srcq, mmsize
    sub        lend, mmsize/2
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse2
MIX_2_TO_1_S16P_FLT
INIT_XMM sse4
MIX_2_TO_1_S16P_FLT

;-----------------------------------------------------------------------------
; void ff_mix_2_to_1_s16p_q8(int16_t **src, int16_t **matrix, int len,
;                            int out_ch, int in_ch);
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal mix_2_to_1_s16p_q8, 3,4,6, src, matrix, len, src1
    mov       src1q, [srcq+gprsize]
    mov        srcq, [srcq]
    sub       src1q, srcq
    mov     matrixq, [matrixq]
    movd         m4, [matrixq]
    movd         m5, [matrixq]
    SPLATW       m4, m4, 0
    SPLATW       m5, m5, 1
    pxor         m0, m0
    punpcklwd    m4, m0
    punpcklwd    m5, m0
    ALIGN 16
.loop:
    mova         m0, [srcq      ]
    mova         m2, [srcq+src1q]
    punpckhwd    m1, m0, m0
    punpcklwd    m0, m0
    punpckhwd    m3, m2, m2
    punpcklwd    m2, m2
    pmaddwd      m0, m4
    pmaddwd      m1, m4
    pmaddwd      m2, m5
    pmaddwd      m3, m5
    paddd        m0, m2
    paddd        m1, m3
    psrad        m0, 8
    psrad        m1, 8
    packssdw     m0, m1
    mova     [srcq], m0
    add        srcq, mmsize
    sub        lend, mmsize/2
    jg .loop
    REP_RET

;-----------------------------------------------------------------------------
; void ff_mix_1_to_2_fltp_flt(float **src, float **matrix, int len,
;                             int out_ch, int in_ch);
;-----------------------------------------------------------------------------

%macro MIX_1_TO_2_FLTP_FLT 0
cglobal mix_1_to_2_fltp_flt, 3,5,4, src0, matrix0, len, src1, matrix1
    mov       src1q, [src0q+gprsize]
    mov       src0q, [src0q]
    sub       src1q, src0q
    mov    matrix1q, [matrix0q+gprsize]
    mov    matrix0q, [matrix0q]
    VBROADCASTSS m2, [matrix0q]
    VBROADCASTSS m3, [matrix1q]
    ALIGN 16
.loop:
    mova         m0, [src0q]
    mulps        m1, m0, m3
    mulps        m0, m0, m2
    mova  [src0q      ], m0
    mova  [src0q+src1q], m1
    add       src0q, mmsize
    sub        lend, mmsize/4
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse
MIX_1_TO_2_FLTP_FLT
%if HAVE_AVX_EXTERNAL
INIT_YMM avx
MIX_1_TO_2_FLTP_FLT
%endif

;-----------------------------------------------------------------------------
; void ff_mix_1_to_2_s16p_flt(int16_t **src, float **matrix, int len,
;                             int out_ch, int in_ch);
;-----------------------------------------------------------------------------

%macro MIX_1_TO_2_S16P_FLT 0
cglobal mix_1_to_2_s16p_flt, 3,5,6, src0, matrix0, len, src1, matrix1
    mov       src1q, [src0q+gprsize]
    mov       src0q, [src0q]
    sub       src1q, src0q
    mov    matrix1q, [matrix0q+gprsize]
    mov    matrix0q, [matrix0q]
    VBROADCASTSS m4, [matrix0q]
    VBROADCASTSS m5, [matrix1q]
    ALIGN 16
.loop:
    mova         m0, [src0q]
    S16_TO_S32_SX 0, 2
    cvtdq2ps     m0, m0
    cvtdq2ps     m2, m2
    mulps        m1, m0, m5
    mulps        m0, m0, m4
    mulps        m3, m2, m5
    mulps        m2, m2, m4
    cvtps2dq     m0, m0
    cvtps2dq     m1, m1
    cvtps2dq     m2, m2
    cvtps2dq     m3, m3
    packssdw     m0, m2
    packssdw     m1, m3
    mova  [src0q      ], m0
    mova  [src0q+src1q], m1
    add       src0q, mmsize
    sub        lend, mmsize/2
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse2
MIX_1_TO_2_S16P_FLT
INIT_XMM sse4
MIX_1_TO_2_S16P_FLT
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
MIX_1_TO_2_S16P_FLT
%endif

;-----------------------------------------------------------------------------
; void ff_mix_3_8_to_1_2_fltp/s16p_flt(float/int16_t **src, float **matrix,
;                                      int len, int out_ch, int in_ch);
;-----------------------------------------------------------------------------

%macro MIX_3_8_TO_1_2_FLT 3 ; %1 = in channels, %2 = out channels, %3 = s16p or fltp
; define some names to make the code clearer
%assign  in_channels %1
%assign out_channels %2
%assign stereo out_channels - 1
%ifidn %3, s16p
    %assign is_s16 1
%else
    %assign is_s16 0
%endif

; determine how many matrix elements must go on the stack vs. mmregs
%assign matrix_elements in_channels * out_channels
%if is_s16
    %if stereo
        %assign needed_mmregs 7
    %else
        %assign needed_mmregs 5
    %endif
%else
    %if stereo
        %assign needed_mmregs 4
    %else
        %assign needed_mmregs 3
    %endif
%endif
%assign matrix_elements_mm num_mmregs - needed_mmregs
%if matrix_elements < matrix_elements_mm
    %assign matrix_elements_mm matrix_elements
%endif
%if matrix_elements_mm < matrix_elements
    %assign matrix_elements_stack matrix_elements - matrix_elements_mm
%else
    %assign matrix_elements_stack 0
%endif
%assign matrix_stack_size matrix_elements_stack * mmsize

%assign needed_stack_size -1 * matrix_stack_size
%if ARCH_X86_32 && in_channels >= 7
%assign needed_stack_size needed_stack_size - 16
%endif

cglobal mix_%1_to_%2_%3_flt, 3,in_channels+2,needed_mmregs+matrix_elements_mm, needed_stack_size, src0, src1, len, src2, src3, src4, src5, src6, src7

; define src pointers on stack if needed
%if matrix_elements_stack > 0 && ARCH_X86_32 && in_channels >= 7
    %define src5m [rsp+matrix_stack_size+0]
    %define src6m [rsp+matrix_stack_size+4]
    %define src7m [rsp+matrix_stack_size+8]
%endif

; load matrix pointers
%define matrix0q r1q
%define matrix1q r3q
%if stereo
    mov      matrix1q, [matrix0q+gprsize]
%endif
    mov      matrix0q, [matrix0q]

; define matrix coeff names
%assign %%i 0
%assign %%j needed_mmregs
%rep in_channels
    %if %%i >= matrix_elements_mm
        CAT_XDEFINE mx_stack_0_, %%i, 1
        CAT_XDEFINE mx_0_, %%i, [rsp+(%%i-matrix_elements_mm)*mmsize]
    %else
        CAT_XDEFINE mx_stack_0_, %%i, 0
        CAT_XDEFINE mx_0_, %%i, m %+ %%j
        %assign %%j %%j+1
    %endif
    %assign %%i %%i+1
%endrep
%if stereo
%assign %%i 0
%rep in_channels
    %if in_channels + %%i >= matrix_elements_mm
        CAT_XDEFINE mx_stack_1_, %%i, 1
        CAT_XDEFINE mx_1_, %%i, [rsp+(in_channels+%%i-matrix_elements_mm)*mmsize]
    %else
        CAT_XDEFINE mx_stack_1_, %%i, 0
        CAT_XDEFINE mx_1_, %%i, m %+ %%j
        %assign %%j %%j+1
    %endif
    %assign %%i %%i+1
%endrep
%endif

; load/splat matrix coeffs
%assign %%i 0
%rep in_channels
    %if mx_stack_0_ %+ %%i
        VBROADCASTSS m0, [matrix0q+4*%%i]
        mova  mx_0_ %+ %%i, m0
    %else
        VBROADCASTSS mx_0_ %+ %%i, [matrix0q+4*%%i]
    %endif
    %if stereo
    %if mx_stack_1_ %+ %%i
        VBROADCASTSS m0, [matrix1q+4*%%i]
        mova  mx_1_ %+ %%i, m0
    %else
        VBROADCASTSS mx_1_ %+ %%i, [matrix1q+4*%%i]
    %endif
    %endif
    %assign %%i %%i+1
%endrep

; load channel pointers to registers as offsets from the first channel pointer
%if ARCH_X86_64
    movsxd       lenq, r2d
%endif
    shl          lenq, 2-is_s16
%assign %%i 1
%rep (in_channels - 1)
    %if ARCH_X86_32 && in_channels >= 7 && %%i >= 5
    mov         src5q, [src0q+%%i*gprsize]
    add         src5q, lenq
    mov         src %+ %%i %+ m, src5q
    %else
    mov         src %+ %%i %+ q, [src0q+%%i*gprsize]
    add         src %+ %%i %+ q, lenq
    %endif
    %assign %%i %%i+1
%endrep
    mov         src0q, [src0q]
    add         src0q, lenq
    neg          lenq
.loop:
; for x86-32 with 7-8 channels we do not have enough gp registers for all src
; pointers, so we have to load some of them from the stack each time
%define copy_src_from_stack ARCH_X86_32 && in_channels >= 7 && %%i >= 5
%if is_s16
    ; mix with s16p input
    mova           m0, [src0q+lenq]
    S16_TO_S32_SX   0, 1
    cvtdq2ps       m0, m0
    cvtdq2ps       m1, m1
    %if stereo
    mulps          m2, m0, mx_1_0
    mulps          m3, m1, mx_1_0
    %endif
    mulps          m0, m0, mx_0_0
    mulps          m1, m1, mx_0_0
%assign %%i 1
%rep (in_channels - 1)
    %if copy_src_from_stack
        %define src_ptr src5q
    %else
        %define src_ptr src %+ %%i %+ q
    %endif
    %if stereo
    %if copy_src_from_stack
    mov       src_ptr, src %+ %%i %+ m
    %endif
    mova           m4, [src_ptr+lenq]
    S16_TO_S32_SX   4, 5
    cvtdq2ps       m4, m4
    cvtdq2ps       m5, m5
    FMULADD_PS     m2, m4, mx_1_ %+ %%i, m2, m6
    FMULADD_PS     m3, m5, mx_1_ %+ %%i, m3, m6
    FMULADD_PS     m0, m4, mx_0_ %+ %%i, m0, m4
    FMULADD_PS     m1, m5, mx_0_ %+ %%i, m1, m5
    %else
    %if copy_src_from_stack
    mov       src_ptr, src %+ %%i %+ m
    %endif
    mova           m2, [src_ptr+lenq]
    S16_TO_S32_SX   2, 3
    cvtdq2ps       m2, m2
    cvtdq2ps       m3, m3
    FMULADD_PS     m0, m2, mx_0_ %+ %%i, m0, m4
    FMULADD_PS     m1, m3, mx_0_ %+ %%i, m1, m4
    %endif
    %assign %%i %%i+1
%endrep
    %if stereo
    cvtps2dq       m2, m2
    cvtps2dq       m3, m3
    packssdw       m2, m3
    mova [src1q+lenq], m2
    %endif
    cvtps2dq       m0, m0
    cvtps2dq       m1, m1
    packssdw       m0, m1
    mova [src0q+lenq], m0
%else
    ; mix with fltp input
    %if stereo || mx_stack_0_0
    mova           m0, [src0q+lenq]
    %endif
    %if stereo
    mulps          m1, m0, mx_1_0
    %endif
    %if stereo || mx_stack_0_0
    mulps          m0, m0, mx_0_0
    %else
    mulps          m0, mx_0_0, [src0q+lenq]
    %endif
%assign %%i 1
%rep (in_channels - 1)
    %if copy_src_from_stack
        %define src_ptr src5q
        mov   src_ptr, src %+ %%i %+ m
    %else
        %define src_ptr src %+ %%i %+ q
    %endif
    ; avoid extra load for mono if matrix is in a mm register
    %if stereo || mx_stack_0_ %+ %%i
    mova           m2, [src_ptr+lenq]
    %endif
    %if stereo
    FMULADD_PS     m1, m2, mx_1_ %+ %%i, m1, m3
    %endif
    %if stereo || mx_stack_0_ %+ %%i
    FMULADD_PS     m0, m2, mx_0_ %+ %%i, m0, m2
    %else
    FMULADD_PS     m0, mx_0_ %+ %%i, [src_ptr+lenq], m0, m1
    %endif
    %assign %%i %%i+1
%endrep
    mova [src0q+lenq], m0
    %if stereo
    mova [src1q+lenq], m1
    %endif
%endif

    add          lenq, mmsize
    jl .loop
; zero ymm high halves
%if mmsize == 32
    vzeroupper
%endif
    RET
%endmacro

%macro MIX_3_8_TO_1_2_FLT_FUNCS 0
%assign %%i 3
%rep 6
    INIT_XMM sse
    MIX_3_8_TO_1_2_FLT %%i, 1, fltp
    MIX_3_8_TO_1_2_FLT %%i, 2, fltp
    INIT_XMM sse2
    MIX_3_8_TO_1_2_FLT %%i, 1, s16p
    MIX_3_8_TO_1_2_FLT %%i, 2, s16p
    INIT_XMM sse4
    MIX_3_8_TO_1_2_FLT %%i, 1, s16p
    MIX_3_8_TO_1_2_FLT %%i, 2, s16p
    ; do not use ymm AVX or FMA4 in x86-32 for 6 or more channels due to stack alignment issues
    %if HAVE_AVX_EXTERNAL
    %if ARCH_X86_64 || %%i < 6
    INIT_YMM avx
    %else
    INIT_XMM avx
    %endif
    MIX_3_8_TO_1_2_FLT %%i, 1, fltp
    MIX_3_8_TO_1_2_FLT %%i, 2, fltp
    INIT_XMM avx
    MIX_3_8_TO_1_2_FLT %%i, 1, s16p
    MIX_3_8_TO_1_2_FLT %%i, 2, s16p
    %endif
    %if HAVE_FMA4_EXTERNAL
    %if ARCH_X86_64 || %%i < 6
    INIT_YMM fma4
    %else
    INIT_XMM fma4
    %endif
    MIX_3_8_TO_1_2_FLT %%i, 1, fltp
    MIX_3_8_TO_1_2_FLT %%i, 2, fltp
    INIT_XMM fma4
    MIX_3_8_TO_1_2_FLT %%i, 1, s16p
    MIX_3_8_TO_1_2_FLT %%i, 2, s16p
    %endif
    %assign %%i %%i+1
%endrep
%endmacro

MIX_3_8_TO_1_2_FLT_FUNCS
