;*****************************************************************************
;* x86-optimized AC-3 downmixing
;* Copyright (c) 2012 Justin Ruggles
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

;******************************************************************************
;* This is based on the channel mixing asm in libavresample, but it is
;* simplified for only float coefficients and only 3 to 6 channels.
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION .text

;-----------------------------------------------------------------------------
; functions to downmix from 3 to 6 channels to mono or stereo
; void ff_ac3_downmix_*(float **samples, float **matrix, int len);
;-----------------------------------------------------------------------------

%macro AC3_DOWNMIX 2 ; %1 = in channels, %2 = out channels
; define some names to make the code clearer
%assign  in_channels %1
%assign out_channels %2
%assign stereo out_channels - 1

; determine how many matrix elements must go on the stack vs. mmregs
%assign matrix_elements in_channels * out_channels
%if stereo
    %assign needed_mmregs 4
%else
    %assign needed_mmregs 3
%endif
%assign matrix_elements_mm num_mmregs - needed_mmregs
%if matrix_elements < matrix_elements_mm
    %assign matrix_elements_mm matrix_elements
%endif
%assign total_mmregs needed_mmregs+matrix_elements_mm
%if matrix_elements_mm < matrix_elements
    %assign matrix_elements_stack matrix_elements - matrix_elements_mm
%else
    %assign matrix_elements_stack 0
%endif

cglobal ac3_downmix_%1_to_%2, 3,in_channels+1,total_mmregs,0-matrix_elements_stack*mmsize, src0, src1, len, src2, src3, src4, src5

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

    lea          lenq, [4*r2d]
    ; load channel pointers to registers
%assign %%i 1
%rep (in_channels - 1)
    mov         src %+ %%i %+ q, [src0q+%%i*gprsize]
    add         src %+ %%i %+ q, lenq
    %assign %%i %%i+1
%endrep
    mov         src0q, [src0q]
    add         src0q, lenq
    neg          lenq
.loop:
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
    %define src_ptr src %+ %%i %+ q
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

    add          lenq, mmsize
    jl .loop
    RET
%endmacro

%macro AC3_DOWNMIX_FUNCS 0
%assign %%i 3
%rep 4
    INIT_XMM sse
    AC3_DOWNMIX %%i, 1
    AC3_DOWNMIX %%i, 2
    INIT_YMM avx
    AC3_DOWNMIX %%i, 1
    AC3_DOWNMIX %%i, 2
    %if HAVE_FMA3_EXTERNAL
    INIT_YMM fma3
    AC3_DOWNMIX %%i, 1
    AC3_DOWNMIX %%i, 2
    %endif
    %assign %%i %%i+1
%endrep
%endmacro

AC3_DOWNMIX_FUNCS
