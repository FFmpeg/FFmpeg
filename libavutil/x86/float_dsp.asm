;*****************************************************************************
;* x86-optimized Float DSP functions
;*
;* Copyright 2006 Loren Merritt
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

%include "x86util.asm"

SECTION .text

;-----------------------------------------------------------------------------
; void vector_fmul(float *dst, const float *src0, const float *src1, int len)
;-----------------------------------------------------------------------------
%macro VECTOR_FMUL 0
cglobal vector_fmul, 4,4,2, dst, src0, src1, len
    lea       lenq, [lend*4 - 2*mmsize]
ALIGN 16
.loop:
    mova      m0,   [src0q + lenq]
    mova      m1,   [src0q + lenq + mmsize]
    mulps     m0, m0, [src1q + lenq]
    mulps     m1, m1, [src1q + lenq + mmsize]
    mova      [dstq + lenq], m0
    mova      [dstq + lenq + mmsize], m1

    sub       lenq, 2*mmsize
    jge       .loop
    REP_RET
%endmacro

INIT_XMM sse
VECTOR_FMUL
%if HAVE_AVX_EXTERNAL
INIT_YMM avx
VECTOR_FMUL
%endif

;------------------------------------------------------------------------------
; void ff_vector_fmac_scalar(float *dst, const float *src, float mul, int len)
;------------------------------------------------------------------------------

%macro VECTOR_FMAC_SCALAR 0
%if UNIX64
cglobal vector_fmac_scalar, 3,3,3, dst, src, len
%else
cglobal vector_fmac_scalar, 4,4,3, dst, src, mul, len
%endif
%if ARCH_X86_32
    VBROADCASTSS m0, mulm
%else
%if WIN64
    mova       xmm0, xmm2
%endif
    shufps     xmm0, xmm0, 0
%if cpuflag(avx)
    vinsertf128  m0, m0, xmm0, 1
%endif
%endif
    lea    lenq, [lend*4-2*mmsize]
.loop:
    mulps    m1, m0, [srcq+lenq       ]
    mulps    m2, m0, [srcq+lenq+mmsize]
    addps    m1, m1, [dstq+lenq       ]
    addps    m2, m2, [dstq+lenq+mmsize]
    mova  [dstq+lenq       ], m1
    mova  [dstq+lenq+mmsize], m2
    sub    lenq, 2*mmsize
    jge .loop
    REP_RET
%endmacro

INIT_XMM sse
VECTOR_FMAC_SCALAR
%if HAVE_AVX_EXTERNAL
INIT_YMM avx
VECTOR_FMAC_SCALAR
%endif

;------------------------------------------------------------------------------
; void ff_vector_fmul_scalar(float *dst, const float *src, float mul, int len)
;------------------------------------------------------------------------------

%macro VECTOR_FMUL_SCALAR 0
%if UNIX64
cglobal vector_fmul_scalar, 3,3,2, dst, src, len
%else
cglobal vector_fmul_scalar, 4,4,3, dst, src, mul, len
%endif
%if ARCH_X86_32
    movss    m0, mulm
%elif WIN64
    SWAP 0, 2
%endif
    shufps   m0, m0, 0
    lea    lenq, [lend*4-mmsize]
.loop:
    mova     m1, [srcq+lenq]
    mulps    m1, m0
    mova  [dstq+lenq], m1
    sub    lenq, mmsize
    jge .loop
    REP_RET
%endmacro

INIT_XMM sse
VECTOR_FMUL_SCALAR

;------------------------------------------------------------------------------
; void ff_vector_dmul_scalar(double *dst, const double *src, double mul,
;                            int len)
;------------------------------------------------------------------------------

%macro VECTOR_DMUL_SCALAR 0
%if ARCH_X86_32
cglobal vector_dmul_scalar, 3,4,3, dst, src, mul, len, lenaddr
    mov          lenq, lenaddrm
%elif UNIX64
cglobal vector_dmul_scalar, 3,3,3, dst, src, len
%else
cglobal vector_dmul_scalar, 4,4,3, dst, src, mul, len
%endif
%if ARCH_X86_32
    VBROADCASTSD   m0, mulm
%else
%if WIN64
    movlhps      xmm2, xmm2
%if cpuflag(avx)
    vinsertf128  ymm2, ymm2, xmm2, 1
%endif
    SWAP 0, 2
%else
    movlhps      xmm0, xmm0
%if cpuflag(avx)
    vinsertf128  ymm0, ymm0, xmm0, 1
%endif
%endif
%endif
    lea          lenq, [lend*8-2*mmsize]
.loop:
    mulpd          m1, m0, [srcq+lenq       ]
    mulpd          m2, m0, [srcq+lenq+mmsize]
    mova   [dstq+lenq       ], m1
    mova   [dstq+lenq+mmsize], m2
    sub          lenq, 2*mmsize
    jge .loop
    REP_RET
%endmacro

INIT_XMM sse2
VECTOR_DMUL_SCALAR
%if HAVE_AVX_EXTERNAL
INIT_YMM avx
VECTOR_DMUL_SCALAR
%endif

;-----------------------------------------------------------------------------
; vector_fmul_add(float *dst, const float *src0, const float *src1,
;                 const float *src2, int len)
;-----------------------------------------------------------------------------
%macro VECTOR_FMUL_ADD 0
cglobal vector_fmul_add, 5,5,2, dst, src0, src1, src2, len
    lea       lenq, [lend*4 - 2*mmsize]
ALIGN 16
.loop:
    mova    m0,   [src0q + lenq]
    mova    m1,   [src0q + lenq + mmsize]
    mulps   m0, m0, [src1q + lenq]
    mulps   m1, m1, [src1q + lenq + mmsize]
    addps   m0, m0, [src2q + lenq]
    addps   m1, m1, [src2q + lenq + mmsize]
    mova    [dstq + lenq], m0
    mova    [dstq + lenq + mmsize], m1

    sub     lenq,   2*mmsize
    jge     .loop
    REP_RET
%endmacro

INIT_XMM sse
VECTOR_FMUL_ADD
%if HAVE_AVX_EXTERNAL
INIT_YMM avx
VECTOR_FMUL_ADD
%endif

;-----------------------------------------------------------------------------
; void vector_fmul_reverse(float *dst, const float *src0, const float *src1,
;                          int len)
;-----------------------------------------------------------------------------
%macro VECTOR_FMUL_REVERSE 0
cglobal vector_fmul_reverse, 4,4,2, dst, src0, src1, len
    lea       lenq, [lend*4 - 2*mmsize]
ALIGN 16
.loop:
%if cpuflag(avx)
    vmovaps     xmm0, [src1q + 16]
    vinsertf128 m0, m0, [src1q], 1
    vshufps     m0, m0, m0, q0123
    vmovaps     xmm1, [src1q + mmsize + 16]
    vinsertf128 m1, m1, [src1q + mmsize], 1
    vshufps     m1, m1, m1, q0123
%else
    mova    m0, [src1q]
    mova    m1, [src1q + mmsize]
    shufps  m0, m0, q0123
    shufps  m1, m1, q0123
%endif
    mulps   m0, m0, [src0q + lenq + mmsize]
    mulps   m1, m1, [src0q + lenq]
    mova    [dstq + lenq + mmsize], m0
    mova    [dstq + lenq], m1
    add     src1q, 2*mmsize
    sub     lenq,  2*mmsize
    jge     .loop
    REP_RET
%endmacro

INIT_XMM sse
VECTOR_FMUL_REVERSE
%if HAVE_AVX_EXTERNAL
INIT_YMM avx
VECTOR_FMUL_REVERSE
%endif

; float scalarproduct_float_sse(const float *v1, const float *v2, int len)
INIT_XMM sse
cglobal scalarproduct_float, 3,3,2, v1, v2, offset
    neg   offsetq
    shl   offsetq, 2
    sub       v1q, offsetq
    sub       v2q, offsetq
    xorps    xmm0, xmm0
.loop:
    movaps   xmm1, [v1q+offsetq]
    mulps    xmm1, [v2q+offsetq]
    addps    xmm0, xmm1
    add   offsetq, 16
    js .loop
    movhlps  xmm1, xmm0
    addps    xmm0, xmm1
    movss    xmm1, xmm0
    shufps   xmm0, xmm0, 1
    addss    xmm0, xmm1
%if ARCH_X86_64 == 0
    movss     r0m,  xmm0
    fld dword r0m
%endif
    RET

;-----------------------------------------------------------------------------
; void ff_butterflies_float(float *src0, float *src1, int len);
;-----------------------------------------------------------------------------
INIT_XMM sse
cglobal butterflies_float, 3,3,3, src0, src1, len
%if ARCH_X86_64
    movsxd    lenq, lend
%endif
    test      lenq, lenq
    jz .end
    shl       lenq, 2
    add      src0q, lenq
    add      src1q, lenq
    neg       lenq
.loop:
    mova        m0, [src0q + lenq]
    mova        m1, [src1q + lenq]
    subps       m2, m0, m1
    addps       m0, m0, m1
    mova        [src1q + lenq], m2
    mova        [src0q + lenq], m0
    add       lenq, mmsize
    jl .loop
.end:
    REP_RET
