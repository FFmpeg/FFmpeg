;******************************************************************************
;* SIMD-optimized functions for the DCA decoder
;* Copyright (C) 2016 James Almer
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

SECTION .text

%define sizeof_float 4
%define FMA3_OFFSET (8 * cpuflag(fma3))

%macro LFE_FIR0_FLOAT 0
cglobal lfe_fir0_float, 4, 6, 12 + cpuflag(fma3)*4, samples, lfe, coeff, nblocks, cnt1, cnt2
    shr nblocksd, 1
    sub     lfeq, 7*sizeof_float
    mov    cnt1d, 32*sizeof_float
    mov    cnt2d, 32*sizeof_float-8-FMA3_OFFSET
    lea   coeffq, [coeffq+cnt1q*8]
    add samplesq, cnt1q
    neg    cnt1q

.loop:
%if cpuflag(avx)
    cvtdq2ps  m4, [lfeq+16]
    cvtdq2ps  m5, [lfeq   ]
    shufps    m7, m4, m4, q0123
    shufps    m6, m5, m5, q0123
%else
    movu      m4, [lfeq+16]
    movu      m5, [lfeq   ]
    cvtdq2ps  m4, m4
    cvtdq2ps  m5, m5
    pshufd    m7, m4, q0123
    pshufd    m6, m5, q0123
%endif

.inner_loop:
%if ARCH_X86_64
    movaps    m8, [coeffq+cnt1q*8   ]
    movaps    m9, [coeffq+cnt1q*8+16]
    movaps   m10, [coeffq+cnt1q*8+32]
    movaps   m11, [coeffq+cnt1q*8+48]
%if cpuflag(fma3)
    movaps   m12, [coeffq+cnt1q*8+64]
    movaps   m13, [coeffq+cnt1q*8+80]
    movaps   m14, [coeffq+cnt1q*8+96]
    movaps   m15, [coeffq+cnt1q*8+112]
    mulps     m0, m7, m8
    mulps     m1, m7, m10
    mulps     m2, m7, m12
    mulps     m3, m7, m14
    fmaddps   m0, m6, m9, m0
    fmaddps   m1, m6, m11, m1
    fmaddps   m2, m6, m13, m2
    fmaddps   m3, m6, m15, m3

    haddps    m0, m1
    haddps    m2, m3
    haddps    m0, m2
    movaps [samplesq+cnt1q], m0
%else
    mulps     m0, m7, m8
    mulps     m1, m6, m9
    mulps     m2, m7, m10
    mulps     m3, m6, m11
    addps     m0, m1
    addps     m2, m3

    unpckhps  m3, m0, m2
    unpcklps  m0, m2
    addps     m3, m0
    movhlps   m2, m3
    addps     m2, m3
    movlps [samplesq+cnt1q], m2
%endif
%else ; ARCH_X86_32
%if cpuflag(fma3)
    mulps     m0, m7, [coeffq+cnt1q*8    ]
    mulps     m1, m7, [coeffq+cnt1q*8+32 ]
    mulps     m2, m7, [coeffq+cnt1q*8+64 ]
    mulps     m3, m7, [coeffq+cnt1q*8+96 ]
    fmaddps   m0, m6, [coeffq+cnt1q*8+16 ], m0
    fmaddps   m1, m6, [coeffq+cnt1q*8+48 ], m1
    fmaddps   m2, m6, [coeffq+cnt1q*8+80 ], m2
    fmaddps   m3, m6, [coeffq+cnt1q*8+112], m3

    haddps    m0, m1
    haddps    m2, m3
    haddps    m0, m2
    movaps [samplesq+cnt1q], m0
%else
    mulps     m0, m7, [coeffq+cnt1q*8   ]
    mulps     m1, m6, [coeffq+cnt1q*8+16]
    mulps     m2, m7, [coeffq+cnt1q*8+32]
    mulps     m3, m6, [coeffq+cnt1q*8+48]
    addps     m0, m1
    addps     m2, m3

    unpckhps  m3, m0, m2
    unpcklps  m0, m2
    addps     m3, m0
    movhlps   m2, m3
    addps     m2, m3
    movlps [samplesq+cnt1q], m2
%endif
%endif; ARCH

%if ARCH_X86_64
%if cpuflag(fma3)
    mulps     m8, m5
    mulps    m10, m5
    mulps    m12, m5
    mulps    m14, m5
    fmaddps   m8, m4, m9, m8
    fmaddps  m10, m4, m11, m10
    fmaddps  m12, m4, m13, m12
    fmaddps  m14, m4, m15, m14

    haddps   m10, m8
    haddps   m14, m12
    haddps   m14, m10
    movaps [samplesq+cnt2q], m14
%else
    mulps     m8, m5
    mulps     m9, m4
    mulps    m10, m5
    mulps    m11, m4
    addps     m8, m9
    addps    m10, m11

    unpckhps m11, m10, m8
    unpcklps m10, m8
    addps    m11, m10
    movhlps   m8, m11
    addps     m8, m11
    movlps [samplesq+cnt2q], m8
%endif
%else ; ARCH_X86_32
%if cpuflag(fma3)
    mulps     m0, m5, [coeffq+cnt1q*8    ]
    mulps     m1, m5, [coeffq+cnt1q*8+32 ]
    mulps     m2, m5, [coeffq+cnt1q*8+64 ]
    mulps     m3, m5, [coeffq+cnt1q*8+96 ]
    fmaddps   m0, m4, [coeffq+cnt1q*8+16 ], m0
    fmaddps   m1, m4, [coeffq+cnt1q*8+48 ], m1
    fmaddps   m2, m4, [coeffq+cnt1q*8+80 ], m2
    fmaddps   m3, m4, [coeffq+cnt1q*8+112], m3

    haddps    m1, m0
    haddps    m3, m2
    haddps    m3, m1
    movaps [samplesq+cnt2q], m3
%else
    mulps     m0, m5, [coeffq+cnt1q*8   ]
    mulps     m1, m4, [coeffq+cnt1q*8+16]
    mulps     m2, m5, [coeffq+cnt1q*8+32]
    mulps     m3, m4, [coeffq+cnt1q*8+48]
    addps     m0, m1
    addps     m2, m3

    unpckhps  m3, m2, m0
    unpcklps  m2, m0
    addps     m3, m2
    movhlps   m0, m3
    addps     m0, m3
    movlps [samplesq+cnt2q], m0
%endif
%endif; ARCH

    sub    cnt2d, 8 + FMA3_OFFSET
    add    cnt1q, 8 + FMA3_OFFSET
    jl .inner_loop

    add     lfeq, 4
    add samplesq,  64*sizeof_float
    mov    cnt1q, -32*sizeof_float
    mov    cnt2d,  32*sizeof_float-8-FMA3_OFFSET
    sub nblocksd, 1
    jg .loop
    RET
%endmacro

INIT_XMM sse2
LFE_FIR0_FLOAT
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
LFE_FIR0_FLOAT
%endif
%if HAVE_FMA3_EXTERNAL
INIT_XMM fma3
LFE_FIR0_FLOAT
%endif

%macro LFE_FIR1_FLOAT 0
cglobal lfe_fir1_float, 4, 6, 10, samples, lfe, coeff, nblocks, cnt1, cnt2
    shr nblocksd, 2
    sub     lfeq, 3*sizeof_float
    mov    cnt1d, 64*sizeof_float
    mov    cnt2d, 64*sizeof_float-16
    lea   coeffq, [coeffq+cnt1q*4]
    add samplesq, cnt1q
    neg    cnt1q

.loop:
%if cpuflag(avx)
    cvtdq2ps  m4, [lfeq]
    shufps    m5, m4, m4, q0123
%else
    movu      m4, [lfeq]
    cvtdq2ps  m4, m4
    pshufd    m5, m4, q0123
%endif

.inner_loop:
    movaps    m6, [coeffq+cnt1q*4   ]
    movaps    m7, [coeffq+cnt1q*4+16]
    mulps     m0, m5, m6
    mulps     m1, m5, m7
%if ARCH_X86_64
    movaps    m8, [coeffq+cnt1q*4+32]
    movaps    m9, [coeffq+cnt1q*4+48]
    mulps     m2, m5, m8
    mulps     m3, m5, m9
%else
    mulps     m2, m5, [coeffq+cnt1q*4+32]
    mulps     m3, m5, [coeffq+cnt1q*4+48]
%endif

    haddps    m0, m1
    haddps    m2, m3
    haddps    m0, m2
    movaps [samplesq+cnt1q], m0

    mulps     m6, m4
    mulps     m7, m4
%if ARCH_X86_64
    mulps     m8, m4
    mulps     m9, m4

    haddps    m6, m7
    haddps    m8, m9
    haddps    m6, m8
%else
    mulps     m2, m4, [coeffq+cnt1q*4+32]
    mulps     m3, m4, [coeffq+cnt1q*4+48]

    haddps    m6, m7
    haddps    m2, m3
    haddps    m6, m2
%endif
    movaps [samplesq+cnt2q], m6

    sub    cnt2d, 16
    add    cnt1q, 16
    jl .inner_loop

    add     lfeq, sizeof_float
    add samplesq, 128*sizeof_float
    mov    cnt1q, -64*sizeof_float
    mov    cnt2d,  64*sizeof_float-16
    sub nblocksd, 1
    jg .loop
    RET
%endmacro

INIT_XMM sse3
LFE_FIR1_FLOAT
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
LFE_FIR1_FLOAT
%endif
