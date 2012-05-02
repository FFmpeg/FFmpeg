;******************************************************************************
;* x86 optimized Format Conversion Utils
;* Copyright (c) 2008 Loren Merritt
;* Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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
%include "util.asm"

SECTION_RODATA 32

pf_s32_inv_scale: times 8 dd 0x30000000
pf_s32_scale:     times 8 dd 0x4f000000
pf_s16_inv_scale: times 4 dd 0x38000000
pf_s16_scale:     times 4 dd 0x47000000

SECTION_TEXT

;------------------------------------------------------------------------------
; void ff_conv_s16_to_s32(int32_t *dst, const int16_t *src, int len);
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal conv_s16_to_s32, 3,3,3, dst, src, len
    lea      lenq, [2*lend]
    lea      dstq, [dstq+2*lenq]
    add      srcq, lenq
    neg      lenq
.loop:
    mova       m2, [srcq+lenq]
    pxor       m0, m0
    pxor       m1, m1
    punpcklwd  m0, m2
    punpckhwd  m1, m2
    mova  [dstq+2*lenq       ], m0
    mova  [dstq+2*lenq+mmsize], m1
    add      lenq, mmsize
    jl .loop
    REP_RET

;------------------------------------------------------------------------------
; void ff_conv_s16_to_flt(float *dst, const int16_t *src, int len);
;------------------------------------------------------------------------------

%macro CONV_S16_TO_FLT 0
cglobal conv_s16_to_flt, 3,3,3, dst, src, len
    lea      lenq, [2*lend]
    add      srcq, lenq
    lea      dstq, [dstq + 2*lenq]
    neg      lenq
    mova       m2, [pf_s16_inv_scale]
    ALIGN 16
.loop:
    mova       m0, [srcq+lenq]
    S16_TO_S32_SX 0, 1
    cvtdq2ps   m0, m0
    cvtdq2ps   m1, m1
    mulps      m0, m2
    mulps      m1, m2
    mova  [dstq+2*lenq       ], m0
    mova  [dstq+2*lenq+mmsize], m1
    add      lenq, mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_S16_TO_FLT
INIT_XMM sse4
CONV_S16_TO_FLT

;------------------------------------------------------------------------------
; void ff_conv_s32_to_s16(int16_t *dst, const int32_t *src, int len);
;------------------------------------------------------------------------------

%macro CONV_S32_TO_S16 0
cglobal conv_s32_to_s16, 3,3,4, dst, src, len
    lea     lenq, [2*lend]
    lea     srcq, [srcq+2*lenq]
    add     dstq, lenq
    neg     lenq
.loop:
    mova      m0, [srcq+2*lenq         ]
    mova      m1, [srcq+2*lenq+  mmsize]
    mova      m2, [srcq+2*lenq+2*mmsize]
    mova      m3, [srcq+2*lenq+3*mmsize]
    psrad     m0, 16
    psrad     m1, 16
    psrad     m2, 16
    psrad     m3, 16
    packssdw  m0, m1
    packssdw  m2, m3
    mova  [dstq+lenq       ], m0
    mova  [dstq+lenq+mmsize], m2
    add     lenq, mmsize*2
    jl .loop
%if mmsize == 8
    emms
    RET
%else
    REP_RET
%endif
%endmacro

INIT_MMX mmx
CONV_S32_TO_S16
INIT_XMM sse2
CONV_S32_TO_S16

;------------------------------------------------------------------------------
; void ff_conv_s32_to_flt(float *dst, const int32_t *src, int len);
;------------------------------------------------------------------------------

%macro CONV_S32_TO_FLT 0
cglobal conv_s32_to_flt, 3,3,3, dst, src, len
    lea     lenq, [4*lend]
    add     srcq, lenq
    add     dstq, lenq
    neg     lenq
    mova      m0, [pf_s32_inv_scale]
    ALIGN 16
.loop:
    cvtdq2ps  m1, [srcq+lenq       ]
    cvtdq2ps  m2, [srcq+lenq+mmsize]
    mulps     m1, m1, m0
    mulps     m2, m2, m0
    mova  [dstq+lenq       ], m1
    mova  [dstq+lenq+mmsize], m2
    add     lenq, mmsize*2
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_S32_TO_FLT
%if HAVE_AVX
INIT_YMM avx
CONV_S32_TO_FLT
%endif

;------------------------------------------------------------------------------
; void ff_conv_flt_to_s16(int16_t *dst, const float *src, int len);
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal conv_flt_to_s16, 3,3,5, dst, src, len
    lea     lenq, [2*lend]
    lea     srcq, [srcq+2*lenq]
    add     dstq, lenq
    neg     lenq
    mova      m4, [pf_s16_scale]
.loop:
    mova      m0, [srcq+2*lenq         ]
    mova      m1, [srcq+2*lenq+1*mmsize]
    mova      m2, [srcq+2*lenq+2*mmsize]
    mova      m3, [srcq+2*lenq+3*mmsize]
    mulps     m0, m4
    mulps     m1, m4
    mulps     m2, m4
    mulps     m3, m4
    cvtps2dq  m0, m0
    cvtps2dq  m1, m1
    cvtps2dq  m2, m2
    cvtps2dq  m3, m3
    packssdw  m0, m1
    packssdw  m2, m3
    mova  [dstq+lenq       ], m0
    mova  [dstq+lenq+mmsize], m2
    add     lenq, mmsize*2
    jl .loop
    REP_RET

;------------------------------------------------------------------------------
; void ff_conv_flt_to_s32(int32_t *dst, const float *src, int len);
;------------------------------------------------------------------------------

%macro CONV_FLT_TO_S32 0
cglobal conv_flt_to_s32, 3,3,5, dst, src, len
    lea     lenq, [lend*4]
    add     srcq, lenq
    add     dstq, lenq
    neg     lenq
    mova      m4, [pf_s32_scale]
.loop:
    mulps     m0, m4, [srcq+lenq         ]
    mulps     m1, m4, [srcq+lenq+1*mmsize]
    mulps     m2, m4, [srcq+lenq+2*mmsize]
    mulps     m3, m4, [srcq+lenq+3*mmsize]
    cvtps2dq  m0, m0
    cvtps2dq  m1, m1
    cvtps2dq  m2, m2
    cvtps2dq  m3, m3
    mova  [dstq+lenq         ], m0
    mova  [dstq+lenq+1*mmsize], m1
    mova  [dstq+lenq+2*mmsize], m2
    mova  [dstq+lenq+3*mmsize], m3
    add     lenq, mmsize*4
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_FLT_TO_S32
%if HAVE_AVX
INIT_YMM avx
CONV_FLT_TO_S32
%endif

;------------------------------------------------------------------------------
; void ff_conv_s16p_to_s16_2ch(int16_t *dst, int16_t *const *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_S16P_TO_S16_2CH 0
cglobal conv_s16p_to_s16_2ch, 3,4,5, dst, src0, len, src1
    mov       src1q, [src0q+gprsize]
    mov       src0q, [src0q        ]
    lea        lenq, [2*lend]
    add       src0q, lenq
    add       src1q, lenq
    lea        dstq, [dstq+2*lenq]
    neg        lenq
.loop
    mova         m0, [src0q+lenq       ]
    mova         m1, [src1q+lenq       ]
    mova         m2, [src0q+lenq+mmsize]
    mova         m3, [src1q+lenq+mmsize]
    SBUTTERFLY2  wd, 0, 1, 4
    SBUTTERFLY2  wd, 2, 3, 4
    mova  [dstq+2*lenq+0*mmsize], m0
    mova  [dstq+2*lenq+1*mmsize], m1
    mova  [dstq+2*lenq+2*mmsize], m2
    mova  [dstq+2*lenq+3*mmsize], m3
    add        lenq, 2*mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_S16P_TO_S16_2CH
%if HAVE_AVX
INIT_XMM avx
CONV_S16P_TO_S16_2CH
%endif

;-----------------------------------------------------------------------------
; void ff_conv_fltp_to_flt_6ch(float *dst, float *const *src, int len,
;                              int channels);
;-----------------------------------------------------------------------------

%macro CONV_FLTP_TO_FLT_6CH 0
cglobal conv_fltp_to_flt_6ch, 2,8,7, dst, src, src1, src2, src3, src4, src5, len
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
    mova      m0, [srcq      ]
    mova      m1, [srcq+src1q]
    mova      m2, [srcq+src2q]
    mova      m3, [srcq+src3q]
    mova      m4, [srcq+src4q]
    mova      m5, [srcq+src5q]
%if cpuflag(sse4)
    SBUTTERFLYPS 0, 1, 6
    SBUTTERFLYPS 2, 3, 6
    SBUTTERFLYPS 4, 5, 6

    blendps   m6, m4, m0, 1100b
    movlhps   m0, m2
    movhlps   m4, m2
    blendps   m2, m5, m1, 1100b
    movlhps   m1, m3
    movhlps   m5, m3

    movaps [dstq   ], m0
    movaps [dstq+16], m6
    movaps [dstq+32], m4
    movaps [dstq+48], m1
    movaps [dstq+64], m2
    movaps [dstq+80], m5
%else ; mmx
    SBUTTERFLY dq, 0, 1, 6
    SBUTTERFLY dq, 2, 3, 6
    SBUTTERFLY dq, 4, 5, 6

    movq   [dstq   ], m0
    movq   [dstq+ 8], m2
    movq   [dstq+16], m4
    movq   [dstq+24], m1
    movq   [dstq+32], m3
    movq   [dstq+40], m5
%endif
    add      srcq, mmsize
    add      dstq, mmsize*6
    sub      lend, mmsize/4
    jg .loop
%if mmsize == 8
    emms
    RET
%else
    REP_RET
%endif
%endmacro

INIT_MMX mmx
CONV_FLTP_TO_FLT_6CH
INIT_XMM sse4
CONV_FLTP_TO_FLT_6CH
%if HAVE_AVX
INIT_XMM avx
CONV_FLTP_TO_FLT_6CH
%endif
