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
    RET
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
cglobal ps_mul_pair_single, 4, 4, 4, dst, src1, src2, n
    shl      nd, 3
    add   src1q, nq
    add    dstq, nq
    neg      nq

align 16
.loop:
    movu     m0, [src1q+nq]
    movu     m1, [src1q+nq+mmsize]
    mova     m2, [src2q]
    mova     m3, m2
    unpcklps m2, m2
    unpckhps m3, m3
    mulps    m0, m2
    mulps    m1, m3
    mova [dstq+nq], m0
    mova [dstq+nq+mmsize], m1
    add   src2q, mmsize
    add      nq, mmsize*2
    jl .loop
    RET

;***********************************************************************
;void ff_ps_stereo_interpolate_sse3(float (*l)[2], float (*r)[2],
;                                   float h[2][4], float h_step[2][4],
;                                   int len);
;***********************************************************************
INIT_XMM sse3
cglobal ps_stereo_interpolate, 5, 5, 6, l, r, h, h_step, n
    movaps   m0, [hq]
    movaps   m1, [h_stepq]
    unpcklps m4, m0, m0
    unpckhps m0, m0
    unpcklps m5, m1, m1
    unpckhps m1, m1
    shl      nd, 3
    add      lq, nq
    add      rq, nq
    neg      nq

align 16
.loop:
    addps    m4, m5
    addps    m0, m1
    movddup  m2, [lq+nq]
    movddup  m3, [rq+nq]
    mulps    m2, m4
    mulps    m3, m0
    addps    m2, m3
    movsd  [lq+nq], m2
    movhps [rq+nq], m2
    add      nq, 8
    jl .loop
    RET

;***************************************************************************
;void ps_stereo_interpolate_ipdopd_sse3(float (*l)[2], float (*r)[2],
;                                       float h[2][4], float h_step[2][4],
;                                       int len);
;***************************************************************************
INIT_XMM sse3
cglobal ps_stereo_interpolate_ipdopd, 5, 5, 10, l, r, h, h_step, n
    movaps   m0, [hq]
    movaps   m1, [hq+mmsize]
%if ARCH_X86_64
    movaps   m8, [h_stepq]
    movaps   m9, [h_stepq+mmsize]
    %define  H_STEP0 m8
    %define  H_STEP1 m9
%else
    %define  H_STEP0 [h_stepq]
    %define  H_STEP1 [h_stepq+mmsize]
%endif
    shl      nd, 3
    add      lq, nq
    add      rq, nq
    neg      nq

align 16
.loop:
    addps    m0, H_STEP0
    addps    m1, H_STEP1
    movddup  m2, [lq+nq]
    movddup  m3, [rq+nq]
    shufps   m4, m2, m2, q2301
    shufps   m5, m3, m3, q2301
    unpcklps m6, m0, m0
    unpckhps m7, m0, m0
    mulps    m2, m6
    mulps    m3, m7
    unpcklps m6, m1, m1
    unpckhps m7, m1, m1
    mulps    m4, m6
    mulps    m5, m7
    addps    m2, m3
    addsubps m2, m4
    addsubps m2, m5
    movsd  [lq+nq], m2
    movhps [rq+nq], m2
    add      nq, 8
    jl .loop
    RET

;**********************************************************
;void ps_hybrid_analysis_ileave_sse(float out[2][38][64],
;                                   float (*in)[32][2],
;                                   int i, int len)
;**********************************************************
INIT_XMM sse
cglobal ps_hybrid_analysis_ileave, 3, 7, 5, out, in, i, len, in0, in1, tmp
    movsxdifnidn        iq, id
    mov               lend, 32 << 3
    lea                inq, [inq+iq*4]
    mov               tmpd, id
    shl               tmpd, 8
    add               outq, tmpq
    mov               tmpd, 64
    sub               tmpd, id
    mov                 id, tmpd

    test                id, 1
    jne .loop4
    test                id, 2
    jne .loop8

align 16
.loop16:
    mov               in0q, inq
    mov               in1q, 38*64*4
    add               in1q, in0q
    mov               tmpd, lend

.inner_loop16:
    movaps              m0, [in0q]
    movaps              m1, [in1q]
    movaps              m2, [in0q+lenq]
    movaps              m3, [in1q+lenq]
    TRANSPOSE4x4PS 0, 1, 2, 3, 4
    movaps          [outq], m0
    movaps     [outq+lenq], m1
    movaps   [outq+lenq*2], m2
    movaps [outq+3*32*2*4], m3
    lea               in0q, [in0q+lenq*2]
    lea               in1q, [in1q+lenq*2]
    add               outq, mmsize
    sub               tmpd, mmsize
    jg .inner_loop16
    add                inq, 16
    add               outq, 3*32*2*4
    sub                 id, 4
    jg .loop16
    RET

align 16
.loop8:
    mov               in0q, inq
    mov               in1q, 38*64*4
    add               in1q, in0q
    mov               tmpd, lend

.inner_loop8:
    movlps              m0, [in0q]
    movlps              m1, [in1q]
    movhps              m0, [in0q+lenq]
    movhps              m1, [in1q+lenq]
    SBUTTERFLYPS 0, 1, 2
    SBUTTERFLYPD 0, 1, 2
    movaps          [outq], m0
    movaps     [outq+lenq], m1
    lea               in0q, [in0q+lenq*2]
    lea               in1q, [in1q+lenq*2]
    add               outq, mmsize
    sub               tmpd, mmsize
    jg .inner_loop8
    add                inq, 8
    add               outq, lenq
    sub                 id, 2
    jg .loop16
    RET

align 16
.loop4:
    mov               in0q, inq
    mov               in1q, 38*64*4
    add               in1q, in0q
    mov               tmpd, lend

.inner_loop4:
    movss               m0, [in0q]
    movss               m1, [in1q]
    movss               m2, [in0q+lenq]
    movss               m3, [in1q+lenq]
    movlhps             m0, m1
    movlhps             m2, m3
    shufps              m0, m2, q2020
    movaps          [outq], m0
    lea               in0q, [in0q+lenq*2]
    lea               in1q, [in1q+lenq*2]
    add               outq, mmsize
    sub               tmpd, mmsize
    jg .inner_loop4
    add                inq, 4
    sub                 id, 1
    test                id, 2
    jne .loop8
    cmp                 id, 4
    jge .loop16
    RET

;***********************************************************
;void ps_hybrid_synthesis_deint_sse4(float out[2][38][64],
;                                    float (*in)[32][2],
;                                    int i, int len)
;***********************************************************
%macro HYBRID_SYNTHESIS_DEINT 0
cglobal ps_hybrid_synthesis_deint, 3, 7, 5, out, in, i, len, out0, out1, tmp
%if cpuflag(sse4)
%define MOVH movsd
%else
%define MOVH movlps
%endif
    movsxdifnidn        iq, id
    mov               lend, 32 << 3
    lea               outq, [outq+iq*4]
    mov               tmpd, id
    shl               tmpd, 8
    add                inq, tmpq
    mov               tmpd, 64
    sub               tmpd, id
    mov                 id, tmpd

    test                id, 1
    jne .loop4
    test                id, 2
    jne .loop8

align 16
.loop16:
    mov              out0q, outq
    mov              out1q, 38*64*4
    add              out1q, out0q
    mov               tmpd, lend

.inner_loop16:
    movaps              m0, [inq]
    movaps              m1, [inq+lenq]
    movaps              m2, [inq+lenq*2]
    movaps              m3, [inq+3*32*2*4]
    TRANSPOSE4x4PS 0, 1, 2, 3, 4
    movaps         [out0q], m0
    movaps         [out1q], m1
    movaps    [out0q+lenq], m2
    movaps    [out1q+lenq], m3
    lea              out0q, [out0q+lenq*2]
    lea              out1q, [out1q+lenq*2]
    add                inq, mmsize
    sub               tmpd, mmsize
    jg .inner_loop16
    add               outq, 16
    add                inq, 3*32*2*4
    sub                 id, 4
    jg .loop16
    RET

align 16
.loop8:
    mov              out0q, outq
    mov              out1q, 38*64*4
    add              out1q, out0q
    mov               tmpd, lend

.inner_loop8:
    movaps              m0, [inq]
    movaps              m1, [inq+lenq]
    SBUTTERFLYPS 0, 1, 2
    SBUTTERFLYPD 0, 1, 2
    MOVH           [out0q], m0
    MOVH           [out1q], m1
    movhps    [out0q+lenq], m0
    movhps    [out1q+lenq], m1
    lea              out0q, [out0q+lenq*2]
    lea              out1q, [out1q+lenq*2]
    add                inq, mmsize
    sub               tmpd, mmsize
    jg .inner_loop8
    add               outq, 8
    add                inq, lenq
    sub                 id, 2
    jg .loop16
    RET

align 16
.loop4:
    mov              out0q, outq
    mov              out1q, 38*64*4
    add              out1q, out0q
    mov               tmpd, lend

.inner_loop4:
    movaps              m0, [inq]
    movss          [out0q], m0
%if cpuflag(sse4)
    extractps      [out1q], m0, 1
    extractps [out0q+lenq], m0, 2
    extractps [out1q+lenq], m0, 3
%else
    movhlps             m1, m0
    movss     [out0q+lenq], m1
    shufps              m0, m0, 0xb1
    movss          [out1q], m0
    movhlps             m1, m0
    movss     [out1q+lenq], m1
%endif
    lea              out0q, [out0q+lenq*2]
    lea              out1q, [out1q+lenq*2]
    add                inq, mmsize
    sub               tmpd, mmsize
    jg .inner_loop4
    add               outq, 4
    sub                 id, 1
    test                id, 2
    jne .loop8
    cmp                 id, 4
    jge .loop16
    RET
%endmacro

INIT_XMM sse
HYBRID_SYNTHESIS_DEINT
INIT_XMM sse4
HYBRID_SYNTHESIS_DEINT

;*******************************************************************
;void ff_ps_hybrid_analysis_<opt>(float (*out)[2], float (*in)[2],
;                                 const float (*filter)[8][2],
;                                 ptrdiff_t stride, int n);
;*******************************************************************
%macro PS_HYBRID_ANALYSIS_IN 1
    movu     m0, [inq+mmsize*%1]
    movu     m1, [inq+mmsize*(5-%1)+8]
    shufps   m3, m0, m0, q2301
    shufps   m4, m1, m1, q0123
    shufps   m1, m1, q1032
%if cpuflag(sse3)
    addsubps m3, m4
    addsubps m0, m1
%else
    xorps    m4, m7
    xorps    m1, m7
    subps    m3, m4
    subps    m0, m1
%endif
    mova  [rsp+mmsize*%1*2], m3
    mova  [rsp+mmsize+mmsize*%1*2], m0
%endmacro

%macro PS_HYBRID_ANALYSIS_LOOP 3
    mova     m2, [filterq+nq+mmsize*%3]
    shufps   m2, m2, q2301
%if cpuflag(fma3)
%if %3
    fmaddps  m3, m2, [rsp+mmsize*%3*2], m3
    fmaddps  m0, m2, [rsp+mmsize+mmsize*%3*2], m0
%else
    mulps    m3, m2, [rsp]
    mulps    m0, m2, [rsp+mmsize]
%endif
%else ; cpuflag(sse)
    mova     %2, [rsp+mmsize*%3*2]
    mova     %1, [rsp+mmsize+mmsize*%3*2]
    mulps    %2, m2
    mulps    %1, m2
%if %3
    addps    m3, %2
    addps    m0, %1
%endif
%endif
%endmacro

%macro PS_HYBRID_ANALYSIS 0
cglobal ps_hybrid_analysis, 5, 5, 5 + notcpuflag(fma3) * 3, 24 * 4, out, in, filter, stride, n
%if cpuflag(sse3)
%define MOVH movsd
%else
%define MOVH movlps
    mova m7, [ps_p1m1p1m1]
%endif
    shl strideq, 3
    shl nd, 6
    add filterq, nq
    neg nq
    PS_HYBRID_ANALYSIS_IN 0
    PS_HYBRID_ANALYSIS_IN 1
    PS_HYBRID_ANALYSIS_IN 2

align 16
.loop:
    PS_HYBRID_ANALYSIS_LOOP m0, m3, 0
    PS_HYBRID_ANALYSIS_LOOP m5, m6, 1
    PS_HYBRID_ANALYSIS_LOOP m5, m6, 2

    shufps   m1, m3, m3, q2301
    shufps   m2, m0, m0, q2301
    subps    m1, m3
    addps    m2, m0
    unpcklps m3, m1, m2
    unpckhps m1, m2
    addps    m1, m3
    movu     m2, [inq+6*8] ; faster than movlps and no risk of overread
    movss    m3, [filterq+nq+8*6]
    SPLATD   m3
%if cpuflag(fma3)
    fmaddps  m1, m2, m3, m1
%else
    mulps    m2, m3
    addps    m1, m2
%endif
    MOVH [outq], m1
    add    outq, strideq
    add      nq, 64
    jl .loop
    RET
%endmacro

INIT_XMM sse
PS_HYBRID_ANALYSIS
INIT_XMM fma3
PS_HYBRID_ANALYSIS
