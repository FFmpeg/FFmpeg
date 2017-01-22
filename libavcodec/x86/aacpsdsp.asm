;******************************************************************************
;* SIMD optimized MPEG-4 Parametric Stereo decoding functions
;*
;* Copyright (C) 2015 James Almer
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

ps_p1m1p1m1: dd 0, 0x80000000, 0, 0x80000000

SECTION .text

;*************************************************************************
;void ff_ps_add_squares_<opt>(float *dst, const float (*src)[2], int n);
;*************************************************************************
%macro PS_ADD_SQUARES 1
cglobal ps_add_squares, 3, 3, %1, dst, src, n
    shl    nd, 3
    add  srcq, nq
    neg    nq

align 16
.loop:
    movaps m0, [srcq+nq]
    movaps m1, [srcq+nq+mmsize]
    mulps  m0, m0
    mulps  m1, m1
    HADDPS m0, m1, m2
    addps  m0, [dstq]
    movaps [dstq], m0
    add  dstq, mmsize
    add    nq, mmsize*2
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse
PS_ADD_SQUARES 2
INIT_XMM sse3
PS_ADD_SQUARES 3

;*******************************************************************
;void ff_ps_mul_pair_single_sse(float (*dst)[2], float (*src0)[2],
;                                   float *src1, int n);
;*******************************************************************
INIT_XMM sse
cglobal ps_mul_pair_single, 4, 5, 4, dst, src1, src2, n
    xor r4q, r4q

.loop:
    movu     m0, [src1q+r4q]
    movu     m1, [src1q+r4q+mmsize]
    mova     m2, [src2q]
    mova     m3, m2
    unpcklps m2, m2
    unpckhps m3, m3
    mulps    m0, m2
    mulps    m1, m3
    mova [dstq+r4q], m0
    mova [dstq+r4q+mmsize], m1
    add   src2q, mmsize
    add     r4q, mmsize*2
    sub      nd, mmsize/4
    jg .loop
    REP_RET

;***********************************************************************
;void ff_ps_stereo_interpolate_sse3(float (*l)[2], float (*r)[2],
;                                   float h[2][4], float h_step[2][4],
;                                   int len);
;***********************************************************************
INIT_XMM sse3
cglobal ps_stereo_interpolate, 5, 5, 6, l, r, h, h_step, n
    movaps   m0, [hq]
    movaps   m1, [h_stepq]
    cmp      nd, 0
    jle .ret
    shl      nd, 3
    add      lq, nq
    add      rq, nq
    neg      nq

align 16
.loop:
    addps    m0, m1
    movddup  m2, [lq+nq]
    movddup  m3, [rq+nq]
    movaps   m4, m0
    movaps   m5, m0
    unpcklps m4, m4
    unpckhps m5, m5
    mulps    m2, m4
    mulps    m3, m5
    addps    m2, m3
    movsd  [lq+nq], m2
    movhps [rq+nq], m2
    add      nq, 8
    jl .loop
.ret:
    REP_RET

;*******************************************************************
;void ff_ps_hybrid_analysis_<opt>(float (*out)[2], float (*in)[2],
;                                 const float (*filter)[8][2],
;                                 int stride, int n);
;*******************************************************************
%macro PS_HYBRID_ANALYSIS_LOOP 3
    movu     %1, [inq+mmsize*%3]
    movu     m1, [inq+mmsize*(5-%3)+8]
%if cpuflag(sse3)
    pshufd   %2, %1, q2301
    pshufd   m4, m1, q0123
    pshufd   m1, m1, q1032
    pshufd   m2, [filterq+nq+mmsize*%3], q2301
    addsubps %2, m4
    addsubps %1, m1
%else
    mova     m2, [filterq+nq+mmsize*%3]
    mova     %2, %1
    mova     m4, m1
    shufps   %2, %2, q2301
    shufps   m4, m4, q0123
    shufps   m1, m1, q1032
    shufps   m2, m2, q2301
    xorps    m4, m7
    xorps    m1, m7
    subps    %2, m4
    subps    %1, m1
%endif
    mulps    %2, m2
    mulps    %1, m2
%if %3
    addps    m3, %2
    addps    m0, %1
%endif
%endmacro

%macro PS_HYBRID_ANALYSIS 0
cglobal ps_hybrid_analysis, 5, 5, 8, out, in, filter, stride, n
%if cpuflag(sse3)
%define MOVH movsd
%else
%define MOVH movlps
%endif
    shl strided, 3
    shl nd, 6
    add filterq, nq
    neg nq
    mova m7, [ps_p1m1p1m1]

align 16
.loop:
    PS_HYBRID_ANALYSIS_LOOP m0, m3, 0
    PS_HYBRID_ANALYSIS_LOOP m5, m6, 1
    PS_HYBRID_ANALYSIS_LOOP m5, m6, 2

%if cpuflag(sse3)
    pshufd   m3, m3, q2301
    xorps    m0, m7
    hsubps   m3, m0
    pshufd   m1, m3, q0020
    pshufd   m3, m3, q0031
    addps    m1, m3
    movsd    m2, [inq+6*8]
%else
    mova     m1, m3
    mova     m2, m0
    shufps   m1, m1, q2301
    shufps   m2, m2, q2301
    subps    m1, m3
    addps    m2, m0
    unpcklps m3, m1, m2
    unpckhps m1, m2
    addps    m1, m3
    movu     m2, [inq+6*8] ; faster than movlps and no risk of overread
%endif
    movss    m3, [filterq+nq+8*6]
    SPLATD   m3
    mulps    m2, m3
    addps    m1, m2
    MOVH [outq], m1
    add    outq, strideq
    add      nq, 64
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse
PS_HYBRID_ANALYSIS
INIT_XMM sse3
PS_HYBRID_ANALYSIS
