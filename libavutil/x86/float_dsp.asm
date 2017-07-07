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

SECTION_RODATA 32
pd_reverse: dd 7, 6, 5, 4, 3, 2, 1, 0

SECTION .text

;-----------------------------------------------------------------------------
; void vector_fmul(float *dst, const float *src0, const float *src1, int len)
;-----------------------------------------------------------------------------
%macro VECTOR_FMUL 0
cglobal vector_fmul, 4,4,2, dst, src0, src1, len
    lea       lenq, [lend*4 - 64]
ALIGN 16
.loop:
%assign a 0
%rep 32/mmsize
    mova      m0,   [src0q + lenq + (a+0)*mmsize]
    mova      m1,   [src0q + lenq + (a+1)*mmsize]
    mulps     m0, m0, [src1q + lenq + (a+0)*mmsize]
    mulps     m1, m1, [src1q + lenq + (a+1)*mmsize]
    mova      [dstq + lenq + (a+0)*mmsize], m0
    mova      [dstq + lenq + (a+1)*mmsize], m1
%assign a a+2
%endrep

    sub       lenq, 64
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
cglobal vector_fmac_scalar, 3,3,5, dst, src, len
%else
cglobal vector_fmac_scalar, 4,4,5, dst, src, mul, len
%endif
%if ARCH_X86_32
    VBROADCASTSS m0, mulm
%else
%if WIN64
    SWAP 0, 2
%endif
    shufps      xm0, xm0, 0
%if cpuflag(avx)
    vinsertf128  m0, m0, xm0, 1
%endif
%endif
    lea    lenq, [lend*4-64]
.loop:
%if cpuflag(fma3)
    mova     m1,     [dstq+lenq]
    mova     m2,     [dstq+lenq+1*mmsize]
    fmaddps  m1, m0, [srcq+lenq], m1
    fmaddps  m2, m0, [srcq+lenq+1*mmsize], m2
%else ; cpuflag
    mulps    m1, m0, [srcq+lenq]
    mulps    m2, m0, [srcq+lenq+1*mmsize]
%if mmsize < 32
    mulps    m3, m0, [srcq+lenq+2*mmsize]
    mulps    m4, m0, [srcq+lenq+3*mmsize]
%endif ; mmsize
    addps    m1, m1, [dstq+lenq]
    addps    m2, m2, [dstq+lenq+1*mmsize]
%if mmsize < 32
    addps    m3, m3, [dstq+lenq+2*mmsize]
    addps    m4, m4, [dstq+lenq+3*mmsize]
%endif ; mmsize
%endif ; cpuflag
    mova  [dstq+lenq], m1
    mova  [dstq+lenq+1*mmsize], m2
%if mmsize < 32
    mova  [dstq+lenq+2*mmsize], m3
    mova  [dstq+lenq+3*mmsize], m4
%endif ; mmsize
    sub    lenq, 64
    jge .loop
    REP_RET
%endmacro

INIT_XMM sse
VECTOR_FMAC_SCALAR
%if HAVE_AVX_EXTERNAL
INIT_YMM avx
VECTOR_FMAC_SCALAR
%endif
%if HAVE_FMA3_EXTERNAL
INIT_YMM fma3
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
; void ff_vector_dmac_scalar(double *dst, const double *src, double mul,
;                            int len)
;------------------------------------------------------------------------------

%macro VECTOR_DMAC_SCALAR 0
%if ARCH_X86_32
cglobal vector_dmac_scalar, 2,4,5, dst, src, mul, len, lenaddr
    mov          lenq, lenaddrm
    VBROADCASTSD m0, mulm
%else
%if UNIX64
cglobal vector_dmac_scalar, 3,3,5, dst, src, len
%else
cglobal vector_dmac_scalar, 4,4,5, dst, src, mul, len
    SWAP 0, 2
%endif
    movlhps     xm0, xm0
%if cpuflag(avx)
    vinsertf128  m0, m0, xm0, 1
%endif
%endif
    lea    lenq, [lend*8-mmsize*4]
.loop:
%if cpuflag(fma3)
    movaps   m1,     [dstq+lenq]
    movaps   m2,     [dstq+lenq+1*mmsize]
    movaps   m3,     [dstq+lenq+2*mmsize]
    movaps   m4,     [dstq+lenq+3*mmsize]
    fmaddpd  m1, m0, [srcq+lenq], m1
    fmaddpd  m2, m0, [srcq+lenq+1*mmsize], m2
    fmaddpd  m3, m0, [srcq+lenq+2*mmsize], m3
    fmaddpd  m4, m0, [srcq+lenq+3*mmsize], m4
%else ; cpuflag
    mulpd    m1, m0, [srcq+lenq]
    mulpd    m2, m0, [srcq+lenq+1*mmsize]
    mulpd    m3, m0, [srcq+lenq+2*mmsize]
    mulpd    m4, m0, [srcq+lenq+3*mmsize]
    addpd    m1, m1, [dstq+lenq]
    addpd    m2, m2, [dstq+lenq+1*mmsize]
    addpd    m3, m3, [dstq+lenq+2*mmsize]
    addpd    m4, m4, [dstq+lenq+3*mmsize]
%endif ; cpuflag
    movaps [dstq+lenq], m1
    movaps [dstq+lenq+1*mmsize], m2
    movaps [dstq+lenq+2*mmsize], m3
    movaps [dstq+lenq+3*mmsize], m4
    sub    lenq, mmsize*4
    jge .loop
    REP_RET
%endmacro

INIT_XMM sse2
VECTOR_DMAC_SCALAR
%if HAVE_AVX_EXTERNAL
INIT_YMM avx
VECTOR_DMAC_SCALAR
%endif
%if HAVE_FMA3_EXTERNAL
INIT_YMM fma3
VECTOR_DMAC_SCALAR
%endif

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
    SWAP 0, 2
%endif
    movlhps       xm0, xm0
%if cpuflag(avx)
    vinsertf128   ym0, ym0, xm0, 1
%endif
%endif
    lea          lenq, [lend*8-2*mmsize]
.loop:
    mulpd          m1, m0, [srcq+lenq       ]
    mulpd          m2, m0, [srcq+lenq+mmsize]
    movaps [dstq+lenq       ], m1
    movaps [dstq+lenq+mmsize], m2
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
; vector_fmul_window(float *dst, const float *src0,
;                    const float *src1, const float *win, int len);
;-----------------------------------------------------------------------------
%macro VECTOR_FMUL_WINDOW 0
cglobal vector_fmul_window, 5, 6, 6, dst, src0, src1, win, len, len1
    shl     lend, 2
    lea    len1q, [lenq - mmsize]
    add    src0q, lenq
    add     dstq, lenq
    add     winq, lenq
    neg     lenq
.loop:
    mova      m0, [winq  + lenq]
    mova      m4, [src0q + lenq]
%if cpuflag(sse)
    mova      m1, [winq  + len1q]
    mova      m5, [src1q + len1q]
    shufps    m1, m1, 0x1b
    shufps    m5, m5, 0x1b
    mova      m2, m0
    mova      m3, m1
    mulps     m2, m4
    mulps     m3, m5
    mulps     m1, m4
    mulps     m0, m5
    addps     m2, m3
    subps     m1, m0
    shufps    m2, m2, 0x1b
%else
    pswapd    m1, [winq  + len1q]
    pswapd    m5, [src1q + len1q]
    mova      m2, m0
    mova      m3, m1
    pfmul     m2, m4
    pfmul     m3, m5
    pfmul     m1, m4
    pfmul     m0, m5
    pfadd     m2, m3
    pfsub     m1, m0
    pswapd    m2, m2
%endif
    mova      [dstq + lenq], m1
    mova      [dstq + len1q], m2
    sub       len1q, mmsize
    add       lenq,  mmsize
    jl .loop
%if mmsize == 8
    femms
%endif
    REP_RET
%endmacro

INIT_MMX 3dnowext
VECTOR_FMUL_WINDOW
INIT_XMM sse
VECTOR_FMUL_WINDOW

;-----------------------------------------------------------------------------
; vector_fmul_add(float *dst, const float *src0, const float *src1,
;                 const float *src2, int len)
;-----------------------------------------------------------------------------
%macro VECTOR_FMUL_ADD 0
cglobal vector_fmul_add, 5,5,4, dst, src0, src1, src2, len
    lea       lenq, [lend*4 - 2*mmsize]
ALIGN 16
.loop:
    mova    m0,   [src0q + lenq]
    mova    m1,   [src0q + lenq + mmsize]
%if cpuflag(fma3)
    mova    m2,     [src2q + lenq]
    mova    m3,     [src2q + lenq + mmsize]
    fmaddps m0, m0, [src1q + lenq], m2
    fmaddps m1, m1, [src1q + lenq + mmsize], m3
%else
    mulps   m0, m0, [src1q + lenq]
    mulps   m1, m1, [src1q + lenq + mmsize]
    addps   m0, m0, [src2q + lenq]
    addps   m1, m1, [src2q + lenq + mmsize]
%endif
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
%if HAVE_FMA3_EXTERNAL
INIT_YMM fma3
VECTOR_FMUL_ADD
%endif

;-----------------------------------------------------------------------------
; void vector_fmul_reverse(float *dst, const float *src0, const float *src1,
;                          int len)
;-----------------------------------------------------------------------------
%macro VECTOR_FMUL_REVERSE 0
cglobal vector_fmul_reverse, 4,4,2, dst, src0, src1, len
%if cpuflag(avx2)
    movaps  m2, [pd_reverse]
%endif
    lea       lenq, [lend*4 - 2*mmsize]
ALIGN 16
.loop:
%if cpuflag(avx2)
    vpermps m0, m2, [src1q]
    vpermps m1, m2, [src1q+mmsize]
%elif cpuflag(avx)
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
    movaps  [dstq + lenq + mmsize], m0
    movaps  [dstq + lenq], m1
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
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
VECTOR_FMUL_REVERSE
%endif

; float scalarproduct_float_sse(const float *v1, const float *v2, int len)
INIT_XMM sse
cglobal scalarproduct_float, 3,3,2, v1, v2, offset
    shl   offsetd, 2
    add       v1q, offsetq
    add       v2q, offsetq
    neg   offsetq
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
    shl       lend, 2
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
    REP_RET
