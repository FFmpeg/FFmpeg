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

SECTION .text

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
; for v0, incrementing and for v1, decrementing
    mova        va, [cf0q + OFFSET]
    mova        vb, [cf0q + OFFSET + 4*NUM_COEF]
%if %0 == 3
    mova        m4, [cf0q + OFFSET + mmsize]
    mova        m0, [cf0q + OFFSET + 4*NUM_COEF + mmsize]
%endif
    mulps       va, %2
    mulps       vb, %2
%if %0 == 3
%if cpuflag(fma3)
    fmaddps     va, m4, %3, va
    fmaddps     vb, m0, %3, vb
%else
    mulps       m4, %3
    mulps       m0, %3
    addps       va, m4
    addps       vb, m0
%endif
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
    movlps  [outq + count], vb
%if %1
    sub       cf0q, 8*NUM_COEF
%endif
    add      count, 8
    jl   .loop%1
%endmacro

; void dca_lfe_fir(float *out, float *in, float *coefs)
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
%if HAVE_FMA3_EXTERNAL
INIT_XMM fma3
DCA_LFE_FIR 0
%endif
