;******************************************************************************
;* x86 optimized channel mixing
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

SECTION_TEXT

;-----------------------------------------------------------------------------
; void ff_mix_2_to_1_fltp_flt(float **src, float **matrix, int len,
;                             int out_ch, int in_ch);
;-----------------------------------------------------------------------------

%macro MIX_2_TO_1_FLTP_FLT 0
cglobal mix_2_to_1_fltp_flt, 3,4,6, src, matrix, len, src1
    mov       src1q, [srcq+gprsize]
    mov        srcq, [srcq        ]
    sub       src1q, srcq
    mov     matrixq, [matrixq  ]
    VBROADCASTSS m4, [matrixq  ]
    VBROADCASTSS m5, [matrixq+4]
    ALIGN 16
.loop:
    mulps        m0, m4, [srcq             ]
    mulps        m1, m5, [srcq+src1q       ]
    mulps        m2, m4, [srcq+      mmsize]
    mulps        m3, m5, [srcq+src1q+mmsize]
    addps        m0, m0, m1
    addps        m2, m2, m3
    mova  [srcq       ], m0
    mova  [srcq+mmsize], m2
    add        srcq, mmsize*2
    sub        lend, mmsize*2/4
    jg .loop
%if mmsize == 32
    vzeroupper
    RET
%else
    REP_RET
%endif
%endmacro

INIT_XMM sse
MIX_2_TO_1_FLTP_FLT
%if HAVE_AVX
INIT_YMM avx
MIX_2_TO_1_FLTP_FLT
%endif

;-----------------------------------------------------------------------------
; void ff_mix_2_to_1_s16p_flt(int16_t **src, float **matrix, int len,
;                             int out_ch, int in_ch);
;-----------------------------------------------------------------------------

%macro MIX_2_TO_1_S16P_FLT 0
cglobal mix_2_to_1_s16p_flt, 3,4,6, src, matrix, len, src1
    mov       src1q, [srcq+gprsize]
    mov        srcq, [srcq]
    sub       src1q, srcq
    mov     matrixq, [matrixq  ]
    VBROADCASTSS m4, [matrixq  ]
    VBROADCASTSS m5, [matrixq+4]
    ALIGN 16
.loop:
    mova         m0, [srcq      ]
    mova         m2, [srcq+src1q]
    S16_TO_S32_SX 0, 1
    S16_TO_S32_SX 2, 3
    cvtdq2ps     m0, m0
    cvtdq2ps     m1, m1
    cvtdq2ps     m2, m2
    cvtdq2ps     m3, m3
    mulps        m0, m4
    mulps        m1, m4
    mulps        m2, m5
    mulps        m3, m5
    addps        m0, m2
    addps        m1, m3
    cvtps2dq     m0, m0
    cvtps2dq     m1, m1
    packssdw     m0, m1
    mova     [srcq], m0
    add        srcq, mmsize
    sub        lend, mmsize/2
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse2
MIX_2_TO_1_S16P_FLT
INIT_XMM sse4
MIX_2_TO_1_S16P_FLT

;-----------------------------------------------------------------------------
; void ff_mix_2_to_1_s16p_q8(int16_t **src, int16_t **matrix, int len,
;                            int out_ch, int in_ch);
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal mix_2_to_1_s16p_q8, 3,4,6, src, matrix, len, src1
    mov       src1q, [srcq+gprsize]
    mov        srcq, [srcq]
    sub       src1q, srcq
    mov     matrixq, [matrixq]
    movd         m4, [matrixq]
    movd         m5, [matrixq]
    SPLATW       m4, m4, 0
    SPLATW       m5, m5, 1
    pxor         m0, m0
    punpcklwd    m4, m0
    punpcklwd    m5, m0
    ALIGN 16
.loop:
    mova         m0, [srcq      ]
    mova         m2, [srcq+src1q]
    punpckhwd    m1, m0, m0
    punpcklwd    m0, m0
    punpckhwd    m3, m2, m2
    punpcklwd    m2, m2
    pmaddwd      m0, m4
    pmaddwd      m1, m4
    pmaddwd      m2, m5
    pmaddwd      m3, m5
    paddd        m0, m2
    paddd        m1, m3
    psrad        m0, 8
    psrad        m1, 8
    packssdw     m0, m1
    mova     [srcq], m0
    add        srcq, mmsize
    sub        lend, mmsize/2
    jg .loop
    REP_RET
