;******************************************************************************
;* x86 optimized Format Conversion Utils
;* Copyright (c) 2008 Loren Merritt
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

SECTION_TEXT

;---------------------------------------------------------------------------------
; void int32_to_float_fmul_scalar(float *dst, const int *src, float mul, int len);
;---------------------------------------------------------------------------------
%macro INT32_TO_FLOAT_FMUL_SCALAR 2
%if UNIX64
cglobal int32_to_float_fmul_scalar_%1, 3,3,%2, dst, src, len
%else
cglobal int32_to_float_fmul_scalar_%1, 4,4,%2, dst, src, mul, len
%endif
%if WIN64
    SWAP 0, 2
%elif ARCH_X86_32
    movss   m0, mulm
%endif
    SPLATD  m0
    shl     lenq, 2
    add     srcq, lenq
    add     dstq, lenq
    neg     lenq
.loop:
%ifidn %1, sse2
    cvtdq2ps  m1, [srcq+lenq   ]
    cvtdq2ps  m2, [srcq+lenq+16]
%else
    cvtpi2ps  m1, [srcq+lenq   ]
    cvtpi2ps  m3, [srcq+lenq+ 8]
    cvtpi2ps  m2, [srcq+lenq+16]
    cvtpi2ps  m4, [srcq+lenq+24]
    movlhps   m1, m3
    movlhps   m2, m4
%endif
    mulps     m1, m0
    mulps     m2, m0
    mova  [dstq+lenq   ], m1
    mova  [dstq+lenq+16], m2
    add     lenq, 32
    jl .loop
    REP_RET
%endmacro

INIT_XMM
%define SPLATD SPLATD_SSE
%define movdqa movaps
INT32_TO_FLOAT_FMUL_SCALAR sse, 5
%undef movdqa
%define SPLATD SPLATD_SSE2
INT32_TO_FLOAT_FMUL_SCALAR sse2, 3
%undef SPLATD


;------------------------------------------------------------------------------
; void ff_float_to_int16(int16_t *dst, const float *src, long len);
;------------------------------------------------------------------------------
%macro FLOAT_TO_INT16 2
cglobal float_to_int16_%1, 3,3,%2, dst, src, len
    add       lenq, lenq
    lea       srcq, [srcq+2*lenq]
    add       dstq, lenq
    neg       lenq
.loop:
%ifidn %1, sse2
    cvtps2dq    m0, [srcq+2*lenq   ]
    cvtps2dq    m1, [srcq+2*lenq+16]
    packssdw    m0, m1
    mova  [dstq+lenq], m0
%else
    cvtps2pi    m0, [srcq+2*lenq   ]
    cvtps2pi    m1, [srcq+2*lenq+ 8]
    cvtps2pi    m2, [srcq+2*lenq+16]
    cvtps2pi    m3, [srcq+2*lenq+24]
    packssdw    m0, m1
    packssdw    m2, m3
    mova  [dstq+lenq  ], m0
    mova  [dstq+lenq+8], m2
%endif
    add       lenq, 16
    js .loop
%ifnidn %1, sse2
    emms
%endif
    REP_RET
%endmacro

INIT_XMM
FLOAT_TO_INT16 sse2, 2
INIT_MMX
FLOAT_TO_INT16 sse, 0
%define cvtps2pi pf2id
FLOAT_TO_INT16 3dnow, 0
%undef cvtps2pi

;------------------------------------------------------------------------------
; void ff_float_to_int16_step(int16_t *dst, const float *src, long len, long step);
;------------------------------------------------------------------------------
%macro FLOAT_TO_INT16_STEP 2
cglobal float_to_int16_step_%1, 4,7,%2, dst, src, len, step, step3, v1, v2
    add       lenq, lenq
    lea       srcq, [srcq+2*lenq]
    lea     step3q, [stepq*3]
    neg       lenq
.loop:
%ifidn %1, sse2
    cvtps2dq    m0, [srcq+2*lenq   ]
    cvtps2dq    m1, [srcq+2*lenq+16]
    packssdw    m0, m1
    movd       v1d, m0
    psrldq      m0, 4
    movd       v2d, m0
    psrldq      m0, 4
    mov     [dstq], v1w
    mov  [dstq+stepq*4], v2w
    shr        v1d, 16
    shr        v2d, 16
    mov  [dstq+stepq*2], v1w
    mov  [dstq+step3q*2], v2w
    lea       dstq, [dstq+stepq*8]
    movd       v1d, m0
    psrldq      m0, 4
    movd       v2d, m0
    mov     [dstq], v1w
    mov  [dstq+stepq*4], v2w
    shr        v1d, 16
    shr        v2d, 16
    mov  [dstq+stepq*2], v1w
    mov  [dstq+step3q*2], v2w
    lea       dstq, [dstq+stepq*8]
%else
    cvtps2pi    m0, [srcq+2*lenq   ]
    cvtps2pi    m1, [srcq+2*lenq+ 8]
    cvtps2pi    m2, [srcq+2*lenq+16]
    cvtps2pi    m3, [srcq+2*lenq+24]
    packssdw    m0, m1
    packssdw    m2, m3
    movd       v1d, m0
    psrlq       m0, 32
    movd       v2d, m0
    mov     [dstq], v1w
    mov  [dstq+stepq*4], v2w
    shr        v1d, 16
    shr        v2d, 16
    mov  [dstq+stepq*2], v1w
    mov  [dstq+step3q*2], v2w
    lea       dstq, [dstq+stepq*8]
    movd       v1d, m2
    psrlq       m2, 32
    movd       v2d, m2
    mov     [dstq], v1w
    mov  [dstq+stepq*4], v2w
    shr        v1d, 16
    shr        v2d, 16
    mov  [dstq+stepq*2], v1w
    mov  [dstq+step3q*2], v2w
    lea       dstq, [dstq+stepq*8]
%endif
    add       lenq, 16
    js .loop
%ifnidn %1, sse2
    emms
%endif
    REP_RET
%endmacro

INIT_XMM
FLOAT_TO_INT16_STEP sse2, 2
INIT_MMX
FLOAT_TO_INT16_STEP sse, 0
%define cvtps2pi pf2id
FLOAT_TO_INT16_STEP 3dnow, 0
%undef cvtps2pi

;-------------------------------------------------------------------------------
; void ff_float_to_int16_interleave2(int16_t *dst, const float **src, long len);
;-------------------------------------------------------------------------------
%macro FLOAT_TO_INT16_INTERLEAVE2 1
cglobal float_to_int16_interleave2_%1, 3,4,2, dst, src0, src1, len
    lea      lenq, [4*r2q]
    mov     src1q, [src0q+gprsize]
    mov     src0q, [src0q]
    add      dstq, lenq
    add     src0q, lenq
    add     src1q, lenq
    neg      lenq
.loop:
%ifidn %1, sse2
    cvtps2dq   m0, [src0q+lenq]
    cvtps2dq   m1, [src1q+lenq]
    packssdw   m0, m1
    movhlps    m1, m0
    punpcklwd  m0, m1
    mova  [dstq+lenq], m0
%else
    cvtps2pi   m0, [src0q+lenq  ]
    cvtps2pi   m1, [src0q+lenq+8]
    cvtps2pi   m2, [src1q+lenq  ]
    cvtps2pi   m3, [src1q+lenq+8]
    packssdw   m0, m1
    packssdw   m2, m3
    mova       m1, m0
    punpcklwd  m0, m2
    punpckhwd  m1, m2
    mova  [dstq+lenq  ], m0
    mova  [dstq+lenq+8], m1
%endif
    add      lenq, 16
    js .loop
%ifnidn %1, sse2
    emms
%endif
    REP_RET
%endmacro

INIT_MMX
%define cvtps2pi pf2id
FLOAT_TO_INT16_INTERLEAVE2 3dnow
%undef cvtps2pi
%define movdqa movaps
FLOAT_TO_INT16_INTERLEAVE2 sse
%undef movdqa
INIT_XMM
FLOAT_TO_INT16_INTERLEAVE2 sse2


%macro PSWAPD_SSE 2
    pshufw %1, %2, 0x4e
%endmacro
%macro PSWAPD_3DN1 2
    movq  %1, %2
    psrlq %1, 32
    punpckldq %1, %2
%endmacro

%macro FLOAT_TO_INT16_INTERLEAVE6 1
; void float_to_int16_interleave6_sse(int16_t *dst, const float **src, int len)
cglobal float_to_int16_interleave6_%1, 2,8,0, dst, src, src1, src2, src3, src4, src5, len
%if ARCH_X86_64
    mov     lend, r2d
%else
    %define lend dword r2m
%endif
    mov src1q, [srcq+1*gprsize]
    mov src2q, [srcq+2*gprsize]
    mov src3q, [srcq+3*gprsize]
    mov src4q, [srcq+4*gprsize]
    mov src5q, [srcq+5*gprsize]
    mov srcq,  [srcq]
    sub src1q, srcq
    sub src2q, srcq
    sub src3q, srcq
    sub src4q, srcq
    sub src5q, srcq
.loop:
    cvtps2pi   mm0, [srcq]
    cvtps2pi   mm1, [srcq+src1q]
    cvtps2pi   mm2, [srcq+src2q]
    cvtps2pi   mm3, [srcq+src3q]
    cvtps2pi   mm4, [srcq+src4q]
    cvtps2pi   mm5, [srcq+src5q]
    packssdw   mm0, mm3
    packssdw   mm1, mm4
    packssdw   mm2, mm5
    pswapd     mm3, mm0
    punpcklwd  mm0, mm1
    punpckhwd  mm1, mm2
    punpcklwd  mm2, mm3
    pswapd     mm3, mm0
    punpckldq  mm0, mm2
    punpckhdq  mm2, mm1
    punpckldq  mm1, mm3
    movq [dstq   ], mm0
    movq [dstq+16], mm2
    movq [dstq+ 8], mm1
    add srcq, 8
    add dstq, 24
    sub lend, 2
    jg .loop
    emms
    RET
%endmacro ; FLOAT_TO_INT16_INTERLEAVE6

%define pswapd PSWAPD_SSE
FLOAT_TO_INT16_INTERLEAVE6 sse
%define cvtps2pi pf2id
%define pswapd PSWAPD_3DN1
FLOAT_TO_INT16_INTERLEAVE6 3dnow
%undef pswapd
FLOAT_TO_INT16_INTERLEAVE6 3dn2
%undef cvtps2pi

;-----------------------------------------------------------------------------
; void ff_float_interleave6(float *dst, const float **src, unsigned int len);
;-----------------------------------------------------------------------------

%macro FLOAT_INTERLEAVE6 2
cglobal float_interleave6_%1, 2,8,%2, dst, src, src1, src2, src3, src4, src5, len
%if ARCH_X86_64
    mov     lend, r2d
%else
    %define lend dword r2m
%endif
    mov    src1q, [srcq+1*gprsize]
    mov    src2q, [srcq+2*gprsize]
    mov    src3q, [srcq+3*gprsize]
    mov    src4q, [srcq+4*gprsize]
    mov    src5q, [srcq+5*gprsize]
    mov     srcq, [srcq]
    sub    src1q, srcq
    sub    src2q, srcq
    sub    src3q, srcq
    sub    src4q, srcq
    sub    src5q, srcq
.loop:
%ifidn %1, sse
    movaps    m0, [srcq]
    movaps    m1, [srcq+src1q]
    movaps    m2, [srcq+src2q]
    movaps    m3, [srcq+src3q]
    movaps    m4, [srcq+src4q]
    movaps    m5, [srcq+src5q]

    SBUTTERFLYPS 0, 1, 6
    SBUTTERFLYPS 2, 3, 6
    SBUTTERFLYPS 4, 5, 6

    movaps    m6, m4
    shufps    m4, m0, 0xe4
    movlhps   m0, m2
    movhlps   m6, m2
    movaps [dstq   ], m0
    movaps [dstq+16], m4
    movaps [dstq+32], m6

    movaps    m6, m5
    shufps    m5, m1, 0xe4
    movlhps   m1, m3
    movhlps   m6, m3
    movaps [dstq+48], m1
    movaps [dstq+64], m5
    movaps [dstq+80], m6
%else ; mmx
    movq       m0, [srcq]
    movq       m1, [srcq+src1q]
    movq       m2, [srcq+src2q]
    movq       m3, [srcq+src3q]
    movq       m4, [srcq+src4q]
    movq       m5, [srcq+src5q]

    SBUTTERFLY dq, 0, 1, 6
    SBUTTERFLY dq, 2, 3, 6
    SBUTTERFLY dq, 4, 5, 6
    movq [dstq   ], m0
    movq [dstq+ 8], m2
    movq [dstq+16], m4
    movq [dstq+24], m1
    movq [dstq+32], m3
    movq [dstq+40], m5
%endif
    add      srcq, mmsize
    add      dstq, mmsize*6
    sub      lend, mmsize/4
    jg .loop
%ifidn %1, mmx
    emms
%endif
    REP_RET
%endmacro

INIT_MMX
FLOAT_INTERLEAVE6 mmx, 0
INIT_XMM
FLOAT_INTERLEAVE6 sse, 7

;-----------------------------------------------------------------------------
; void ff_float_interleave2(float *dst, const float **src, unsigned int len);
;-----------------------------------------------------------------------------

%macro FLOAT_INTERLEAVE2 2
cglobal float_interleave2_%1, 3,4,%2, dst, src, len, src1
    mov     src1q, [srcq+gprsize]
    mov      srcq, [srcq        ]
    sub     src1q, srcq
.loop
    MOVPS      m0, [srcq             ]
    MOVPS      m1, [srcq+src1q       ]
    MOVPS      m3, [srcq      +mmsize]
    MOVPS      m4, [srcq+src1q+mmsize]

    MOVPS      m2, m0
    PUNPCKLDQ  m0, m1
    PUNPCKHDQ  m2, m1

    MOVPS      m1, m3
    PUNPCKLDQ  m3, m4
    PUNPCKHDQ  m1, m4

    MOVPS [dstq         ], m0
    MOVPS [dstq+1*mmsize], m2
    MOVPS [dstq+2*mmsize], m3
    MOVPS [dstq+3*mmsize], m1

    add      srcq, mmsize*2
    add      dstq, mmsize*4
    sub      lend, mmsize/2
    jg .loop
%ifidn %1, mmx
    emms
%endif
    REP_RET
%endmacro

INIT_MMX
%define MOVPS     movq
%define PUNPCKLDQ punpckldq
%define PUNPCKHDQ punpckhdq
FLOAT_INTERLEAVE2 mmx, 0
INIT_XMM
%define MOVPS     movaps
%define PUNPCKLDQ unpcklps
%define PUNPCKHDQ unpckhps
FLOAT_INTERLEAVE2 sse, 5
