;******************************************************************************
;* SIMD-optimized JPEG2000 DSP functions
;* Copyright (c) 2014 Nicolas Bertrand
;* Copyright (c) 2015 James Almer
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

SECTION_RODATA 32

pf_ict0: times 8 dd 1.402
pf_ict1: times 8 dd 0.34413
pf_ict2: times 8 dd 0.71414
pf_ict3: times 8 dd 1.772

SECTION .text

;***********************************************************************
; ff_ict_float_<opt>(float *src0, float *src1, float *src2, int csize)
;***********************************************************************
%macro ICT_FLOAT 1
cglobal ict_float, 4, 4, %1, src0, src1, src2, csize
    shl  csized, 2
    add   src0q, csizeq
    add   src1q, csizeq
    add   src2q, csizeq
    neg  csizeq
    movaps   m6, [pf_ict0]
    movaps   m7, [pf_ict1]
    %define ICT0 m6
    %define ICT1 m7

%if ARCH_X86_64
    movaps   m8, [pf_ict2]
    %define ICT2 m8
%if cpuflag(avx)
    movaps   m3, [pf_ict3]
    %define ICT3 m3
%else
    movaps   m9, [pf_ict3]
    %define ICT3 m9
%endif

%else ; ARCH_X86_32
    %define ICT2 [pf_ict2]
%if cpuflag(avx)
    movaps   m3, [pf_ict3]
    %define ICT3 m3
%else
    %define ICT3 [pf_ict3]
%endif

%endif ; ARCH

align 16
.loop:
    movaps   m0, [src0q+csizeq]
    movaps   m1, [src1q+csizeq]
    movaps   m2, [src2q+csizeq]

%if cpuflag(fma4) || cpuflag(fma3)
%if cpuflag(fma4)
    fnmaddps  m5, m1, ICT1, m0
    fmaddps   m4, m2, ICT0, m0
%else ; fma3
    movaps    m5, m1
    movaps    m4, m2
    fnmaddps  m5, m5, ICT1, m0
    fmaddps   m4, m4, ICT0, m0
%endif
    fmaddps   m0, m1, ICT3, m0
    fnmaddps  m5, m2, ICT2, m5
%else ; non FMA
%if cpuflag(avx)
    mulps    m5, m1, ICT1
    mulps    m4, m2, ICT0
    mulps    m1, m1, ICT3
    mulps    m2, m2, ICT2
    subps    m5, m0, m5
%else ; sse
    movaps   m3, m1
    movaps   m4, m2
    movaps   m5, m0
    mulps    m3, ICT1
    mulps    m4, ICT0
    mulps    m1, ICT3
    mulps    m2, ICT2
    subps    m5, m3
%endif
    addps    m4, m4, m0
    addps    m0, m0, m1
    subps    m5, m5, m2
%endif

    movaps   [src0q+csizeq], m4
    movaps   [src2q+csizeq], m0
    movaps   [src1q+csizeq], m5
    add  csizeq, mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse
ICT_FLOAT 10
INIT_YMM avx
ICT_FLOAT 9
%if HAVE_FMA4_EXTERNAL
INIT_XMM fma4
ICT_FLOAT 9
%endif
INIT_YMM fma3
ICT_FLOAT 9

;***************************************************************************
; ff_rct_int_<opt>(int32_t *src0, int32_t *src1, int32_t *src2, int csize)
;***************************************************************************
%macro RCT_INT 0
cglobal rct_int, 4, 4, 4, src0, src1, src2, csize
    shl  csized, 2
    add   src0q, csizeq
    add   src1q, csizeq
    add   src2q, csizeq
    neg  csizeq

align 16
.loop:
    mova   m1, [src1q+csizeq]
    mova   m2, [src2q+csizeq]
    mova   m0, [src0q+csizeq]
    paddd  m3, m1, m2
    psrad  m3, 2
    psubd  m0, m3
    paddd  m1, m0
    paddd  m2, m0
    mova   [src1q+csizeq], m0
    mova   [src2q+csizeq], m1
    mova   [src0q+csizeq], m2
    add  csizeq, mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
RCT_INT
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
RCT_INT
%endif
