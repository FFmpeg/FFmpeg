;******************************************************************************
;* SSE-optimized functions for the DCA decoder
;* Copyright (C) 2012-2014 Christophe Gisquet <christophe.gisquet@gmail.com>
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
pf_inv16:  times 4 dd 0x3D800000 ; 1/16

SECTION_TEXT

; void int8x8_fmul_int32_sse2(float *dst, const int8_t *src, int scale)
%macro INT8X8_FMUL_INT32 0
cglobal int8x8_fmul_int32, 3,3,5, dst, src, scale
    cvtsi2ss    m0, scalem
    mulss       m0, [pf_inv16]
    shufps      m0, m0, 0
%if cpuflag(sse2)
%if cpuflag(sse4)
    pmovsxbd    m1, [srcq+0]
    pmovsxbd    m2, [srcq+4]
%else
    movq        m1, [srcq]
    punpcklbw   m1, m1
    mova        m2, m1
    punpcklwd   m1, m1
    punpckhwd   m2, m2
    psrad       m1, 24
    psrad       m2, 24
%endif
    cvtdq2ps    m1, m1
    cvtdq2ps    m2, m2
%else
    movd       mm0, [srcq+0]
    movd       mm1, [srcq+4]
    punpcklbw  mm0, mm0
    punpcklbw  mm1, mm1
    movq       mm2, mm0
    movq       mm3, mm1
    punpcklwd  mm0, mm0
    punpcklwd  mm1, mm1
    punpckhwd  mm2, mm2
    punpckhwd  mm3, mm3
    psrad      mm0, 24
    psrad      mm1, 24
    psrad      mm2, 24
    psrad      mm3, 24
    cvtpi2ps    m1, mm0
    cvtpi2ps    m2, mm1
    cvtpi2ps    m3, mm2
    cvtpi2ps    m4, mm3
    shufps      m0, m0, 0
    emms
    shufps      m1, m3, q1010
    shufps      m2, m4, q1010
%endif
    mulps       m1, m0
    mulps       m2, m0
    mova [dstq+ 0], m1
    mova [dstq+16], m2
    REP_RET
%endmacro

%if ARCH_X86_32
INIT_XMM sse
INT8X8_FMUL_INT32
%endif

INIT_XMM sse2
INT8X8_FMUL_INT32

INIT_XMM sse4
INT8X8_FMUL_INT32

; %1=v0/v1  %2=in1  %3=in2
%macro FIR_LOOP 2-3
.loop%1:
%define va          m1
%define vb          m2
%if %1
%define OFFSET      0
%else
%define OFFSET      NUM_COEF*count
%endif
; for v0, incrementint and for v1, decrementing
    mova        va, [cf0q + OFFSET]
    mova        vb, [cf0q + OFFSET + 4*NUM_COEF]
%if %0 == 3
    mova        m4, [cf0q + OFFSET + mmsize]
    mova        m0, [cf0q + OFFSET + 4*NUM_COEF + mmsize]
%endif
    mulps       va, %2
    mulps       vb, %2
%if %0 == 3
    mulps       m4, %3
    mulps       m0, %3
    addps       va, m4
    addps       vb, m0
%endif
    ; va = va1 va2 va3 va4
    ; vb = vb1 vb2 vb3 vb4
%if %1
    SWAP        va, vb
%endif
    mova        m4, va
    unpcklps    va, vb ; va3 vb3 va4 vb4
    unpckhps    m4, vb ; va1 vb1 va2 vb2
    addps       m4, va ; va1+3 vb1+3 va2+4 vb2+4
    movhlps     vb, m4 ; va1+3  vb1+3
    addps       vb, m4 ; va0..4 vb0..4
    movh    [outq + count], vb
%if %1
    sub       cf0q, 8*NUM_COEF
%endif
    add      count, 8
    jl   .loop%1
%endmacro

; dca_lfe_fir(float *out, float *in, float *coefs)
%macro DCA_LFE_FIR 1
cglobal dca_lfe_fir%1, 3,3,6-%1, out, in, cf0
%define IN1       m3
%define IN2       m5
%define count     inq
%define NUM_COEF  4*(2-%1)
%define NUM_OUT   32*(%1+1)

    movu     IN1, [inq + 4 - 1*mmsize]
    shufps   IN1, IN1, q0123
%if %1 == 0
    movu     IN2, [inq + 4 - 2*mmsize]
    shufps   IN2, IN2, q0123
%endif

    mov    count, -4*NUM_OUT
    add     cf0q, 4*NUM_COEF*NUM_OUT
    add     outq, 4*NUM_OUT
    ; compute v0 first
%if %1 == 0
    FIR_LOOP   0, IN1, IN2
%else
    FIR_LOOP   0, IN1
%endif
    shufps   IN1, IN1, q0123
    mov    count, -4*NUM_OUT
    ; cf1 already correctly positioned
    add     outq, 4*NUM_OUT          ; outq now at out2
    sub     cf0q, 8*NUM_COEF
%if %1 == 0
    shufps   IN2, IN2, q0123
    FIR_LOOP   1, IN2, IN1
%else
    FIR_LOOP   1, IN1
%endif
    RET
%endmacro

INIT_XMM sse
DCA_LFE_FIR 0
DCA_LFE_FIR 1
