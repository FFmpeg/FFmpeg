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

%include "libavutil/x86/x86util.asm"
%include "util.asm"

SECTION_RODATA 32

pf_s32_inv_scale: times 8 dd 0x30000000
pf_s32_scale:     times 8 dd 0x4f000000
pf_s32_clip:      times 8 dd 0x4effffff
pf_s16_inv_scale: times 4 dd 0x38000000
pf_s16_scale:     times 4 dd 0x47000000
pb_shuf_unpack_even:      db -1, -1,  0,  1, -1, -1,  2,  3, -1, -1,  8,  9, -1, -1, 10, 11
pb_shuf_unpack_odd:       db -1, -1,  4,  5, -1, -1,  6,  7, -1, -1, 12, 13, -1, -1, 14, 15
pb_interleave_words: SHUFFLE_MASK_W  0,  4,  1,  5,  2,  6,  3,  7
pb_deinterleave_words: SHUFFLE_MASK_W  0,  2,  4,  6,  1,  3,  5,  7
pw_zero_even:     times 4 dw 0x0000, 0xffff

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
%if HAVE_AVX_EXTERNAL
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
cglobal conv_flt_to_s32, 3,3,6, dst, src, len
    lea     lenq, [lend*4]
    add     srcq, lenq
    add     dstq, lenq
    neg     lenq
    mova      m4, [pf_s32_scale]
    mova      m5, [pf_s32_clip]
.loop:
    mulps     m0, m4, [srcq+lenq         ]
    mulps     m1, m4, [srcq+lenq+1*mmsize]
    mulps     m2, m4, [srcq+lenq+2*mmsize]
    mulps     m3, m4, [srcq+lenq+3*mmsize]
    minps     m0, m0, m5
    minps     m1, m1, m5
    minps     m2, m2, m5
    minps     m3, m3, m5
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
%if HAVE_AVX_EXTERNAL
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
.loop:
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
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_S16P_TO_S16_2CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_s16p_to_s16_6ch(int16_t *dst, int16_t *const *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

;------------------------------------------------------------------------------
; NOTE: In the 6-channel functions, len could be used as an index on x86-64
;       instead of just a counter, which would avoid incrementing the
;       pointers, but the extra complexity and amount of code is not worth
;       the small gain. On x86-32 there are not enough registers to use len
;       as an index without keeping two of the pointers on the stack and
;       loading them in each iteration.
;------------------------------------------------------------------------------

%macro CONV_S16P_TO_S16_6CH 0
%if ARCH_X86_64
cglobal conv_s16p_to_s16_6ch, 3,8,7, dst, src0, len, src1, src2, src3, src4, src5
%else
cglobal conv_s16p_to_s16_6ch, 2,7,7, dst, src0, src1, src2, src3, src4, src5
%define lend dword r2m
%endif
    mov      src1q, [src0q+1*gprsize]
    mov      src2q, [src0q+2*gprsize]
    mov      src3q, [src0q+3*gprsize]
    mov      src4q, [src0q+4*gprsize]
    mov      src5q, [src0q+5*gprsize]
    mov      src0q, [src0q]
    sub      src1q, src0q
    sub      src2q, src0q
    sub      src3q, src0q
    sub      src4q, src0q
    sub      src5q, src0q
.loop:
%if cpuflag(sse2slow)
    movq        m0, [src0q      ]   ; m0 =  0,  6, 12, 18,  x,  x,  x,  x
    movq        m1, [src0q+src1q]   ; m1 =  1,  7, 13, 19,  x,  x,  x,  x
    movq        m2, [src0q+src2q]   ; m2 =  2,  8, 14, 20,  x,  x,  x,  x
    movq        m3, [src0q+src3q]   ; m3 =  3,  9, 15, 21,  x,  x,  x,  x
    movq        m4, [src0q+src4q]   ; m4 =  4, 10, 16, 22,  x,  x,  x,  x
    movq        m5, [src0q+src5q]   ; m5 =  5, 11, 17, 23,  x,  x,  x,  x
                                    ; unpack words:
    punpcklwd   m0, m1              ; m0 =  0,  1,  6,  7, 12, 13, 18, 19
    punpcklwd   m2, m3              ; m2 =  4,  5, 10, 11, 16, 17, 22, 23
    punpcklwd   m4, m5              ; m4 =  2,  3,  8,  9, 14, 15, 20, 21
                                    ; blend dwords
    shufps      m1, m0, m2, q2020   ; m1 =  0,  1, 12, 13,  2,  3, 14, 15
    shufps      m0, m4, q2031       ; m0 =  6,  7, 18, 19,  4,  5, 16, 17
    shufps      m2, m4, q3131       ; m2 =  8,  9, 20, 21, 10, 11, 22, 23
                                    ; shuffle dwords
    pshufd      m0, m0, q1302       ; m0 =  4,  5,  6,  7, 16, 17, 18, 19
    pshufd      m1, m1, q3120       ; m1 =  0,  1,  2,  3, 12, 13, 14, 15
    pshufd      m2, m2, q3120       ; m2 =  8,  9, 10, 11, 20, 21, 22, 23
    movq   [dstq+0*mmsize/2], m1
    movq   [dstq+1*mmsize/2], m0
    movq   [dstq+2*mmsize/2], m2
    movhps [dstq+3*mmsize/2], m1
    movhps [dstq+4*mmsize/2], m0
    movhps [dstq+5*mmsize/2], m2
    add      src0q, mmsize/2
    add       dstq, mmsize*3
    sub       lend, mmsize/4
%else
    mova        m0, [src0q      ]   ; m0 =  0,  6, 12, 18, 24, 30, 36, 42
    mova        m1, [src0q+src1q]   ; m1 =  1,  7, 13, 19, 25, 31, 37, 43
    mova        m2, [src0q+src2q]   ; m2 =  2,  8, 14, 20, 26, 32, 38, 44
    mova        m3, [src0q+src3q]   ; m3 =  3,  9, 15, 21, 27, 33, 39, 45
    mova        m4, [src0q+src4q]   ; m4 =  4, 10, 16, 22, 28, 34, 40, 46
    mova        m5, [src0q+src5q]   ; m5 =  5, 11, 17, 23, 29, 35, 41, 47
                                    ; unpack words:
    SBUTTERFLY2 wd, 0, 1, 6         ; m0 =  0,  1,  6,  7, 12, 13, 18, 19
                                    ; m1 = 24, 25, 30, 31, 36, 37, 42, 43
    SBUTTERFLY2 wd, 2, 3, 6         ; m2 =  2,  3,  8,  9, 14, 15, 20, 21
                                    ; m3 = 26, 27, 32, 33, 38, 39, 44, 45
    SBUTTERFLY2 wd, 4, 5, 6         ; m4 =  4,  5, 10, 11, 16, 17, 22, 23
                                    ; m5 = 28, 29, 34, 35, 40, 41, 46, 47
                                    ; blend dwords
    shufps      m6, m0, m2, q2020   ; m6 =  0,  1, 12, 13,  2,  3, 14, 15
    shufps      m0, m4, q2031       ; m0 =  6,  7, 18, 19,  4,  5, 16, 17
    shufps      m2, m4, q3131       ; m2 =  8,  9, 20, 21, 10, 11, 22, 23
    SWAP 4,6                        ; m4 =  0,  1, 12, 13,  2,  3, 14, 15
    shufps      m6, m1, m3, q2020   ; m6 = 24, 25, 36, 37, 26, 27, 38, 39
    shufps      m1, m5, q2031       ; m1 = 30, 31, 42, 43, 28, 29, 40, 41
    shufps      m3, m5, q3131       ; m3 = 32, 33, 44, 45, 34, 35, 46, 47
    SWAP 5,6                        ; m5 = 24, 25, 36, 37, 26, 27, 38, 39
                                    ; shuffle dwords
    pshufd      m0, m0, q1302       ; m0 =  4,  5,  6,  7, 16, 17, 18, 19
    pshufd      m2, m2, q3120       ; m2 =  8,  9, 10, 11, 20, 21, 22, 23
    pshufd      m4, m4, q3120       ; m4 =  0,  1,  2,  3, 12, 13, 14, 15
    pshufd      m1, m1, q1302       ; m1 = 28, 29, 30, 31, 40, 41, 42, 43
    pshufd      m3, m3, q3120       ; m3 = 32, 33, 34, 35, 44, 45, 46, 47
    pshufd      m5, m5, q3120       ; m5 = 24, 25, 26, 27, 36, 37, 38, 39
                                    ; shuffle qwords
    punpcklqdq  m6, m4, m0          ; m6 =  0,  1,  2,  3,  4,  5,  6,  7
    punpckhqdq  m0, m2              ; m0 = 16, 17, 18, 19, 20, 21, 22, 23
    shufps      m2, m4, q3210       ; m2 =  8,  9, 10, 11, 12, 13, 14, 15
    SWAP 4,6                        ; m4 =  0,  1,  2,  3,  4,  5,  6,  7
    punpcklqdq  m6, m5, m1          ; m6 = 24, 25, 26, 27, 28, 29, 30, 31
    punpckhqdq  m1, m3              ; m1 = 40, 41, 42, 43, 44, 45, 46, 47
    shufps      m3, m5, q3210       ; m3 = 32, 33, 34, 35, 36, 37, 38, 39
    SWAP 5,6                        ; m5 = 24, 25, 26, 27, 28, 29, 30, 31
    mova   [dstq+0*mmsize], m4
    mova   [dstq+1*mmsize], m2
    mova   [dstq+2*mmsize], m0
    mova   [dstq+3*mmsize], m5
    mova   [dstq+4*mmsize], m3
    mova   [dstq+5*mmsize], m1
    add      src0q, mmsize
    add       dstq, mmsize*6
    sub       lend, mmsize/2
%endif
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_S16P_TO_S16_6CH
INIT_XMM sse2slow
CONV_S16P_TO_S16_6CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_S16P_TO_S16_6CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_s16p_to_flt_2ch(float *dst, int16_t *const *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_S16P_TO_FLT_2CH 0
cglobal conv_s16p_to_flt_2ch, 3,4,6, dst, src0, len, src1
    lea       lenq, [2*lend]
    mov      src1q, [src0q+gprsize]
    mov      src0q, [src0q        ]
    lea       dstq, [dstq+4*lenq]
    add      src0q, lenq
    add      src1q, lenq
    neg       lenq
    mova        m5, [pf_s32_inv_scale]
.loop:
    mova        m2, [src0q+lenq]    ; m2 =  0,  2,  4,  6,  8, 10, 12, 14
    mova        m4, [src1q+lenq]    ; m4 =  1,  3,  5,  7,  9, 11, 13, 15
    SBUTTERFLY2 wd, 2, 4, 3         ; m2 =  0,  1,  2,  3,  4,  5,  6,  7
                                    ; m4 =  8,  9, 10, 11, 12, 13, 14, 15
    pxor        m3, m3
    punpcklwd   m0, m3, m2          ; m0 =      0,      1,      2,      3
    punpckhwd   m1, m3, m2          ; m1 =      4,      5,      6,      7
    punpcklwd   m2, m3, m4          ; m2 =      8,      9,     10,     11
    punpckhwd   m3, m4              ; m3 =     12,     13,     14,     15
    cvtdq2ps    m0, m0
    cvtdq2ps    m1, m1
    cvtdq2ps    m2, m2
    cvtdq2ps    m3, m3
    mulps       m0, m5
    mulps       m1, m5
    mulps       m2, m5
    mulps       m3, m5
    mova  [dstq+4*lenq         ], m0
    mova  [dstq+4*lenq+  mmsize], m1
    mova  [dstq+4*lenq+2*mmsize], m2
    mova  [dstq+4*lenq+3*mmsize], m3
    add       lenq, mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_S16P_TO_FLT_2CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_S16P_TO_FLT_2CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_s16p_to_flt_6ch(float *dst, int16_t *const *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_S16P_TO_FLT_6CH 0
%if ARCH_X86_64
cglobal conv_s16p_to_flt_6ch, 3,8,8, dst, src, len, src1, src2, src3, src4, src5
%else
cglobal conv_s16p_to_flt_6ch, 2,7,8, dst, src, src1, src2, src3, src4, src5
%define lend dword r2m
%endif
    mov     src1q, [srcq+1*gprsize]
    mov     src2q, [srcq+2*gprsize]
    mov     src3q, [srcq+3*gprsize]
    mov     src4q, [srcq+4*gprsize]
    mov     src5q, [srcq+5*gprsize]
    mov      srcq, [srcq]
    sub     src1q, srcq
    sub     src2q, srcq
    sub     src3q, srcq
    sub     src4q, srcq
    sub     src5q, srcq
    mova       m7, [pf_s32_inv_scale]
%if cpuflag(ssse3)
    %define unpack_even m6
    mova       m6, [pb_shuf_unpack_even]
%if ARCH_X86_64
    %define unpack_odd m8
    mova       m8, [pb_shuf_unpack_odd]
%else
    %define unpack_odd [pb_shuf_unpack_odd]
%endif
%endif
.loop:
    movq       m0, [srcq      ]  ; m0 =  0,  6, 12, 18,  x,  x,  x,  x
    movq       m1, [srcq+src1q]  ; m1 =  1,  7, 13, 19,  x,  x,  x,  x
    movq       m2, [srcq+src2q]  ; m2 =  2,  8, 14, 20,  x,  x,  x,  x
    movq       m3, [srcq+src3q]  ; m3 =  3,  9, 15, 21,  x,  x,  x,  x
    movq       m4, [srcq+src4q]  ; m4 =  4, 10, 16, 22,  x,  x,  x,  x
    movq       m5, [srcq+src5q]  ; m5 =  5, 11, 17, 23,  x,  x,  x,  x
                                 ; unpack words:
    punpcklwd  m0, m1            ; m0 =  0,  1,  6,  7, 12, 13, 18, 19
    punpcklwd  m2, m3            ; m2 =  2,  3,  8,  9, 14, 15, 20, 21
    punpcklwd  m4, m5            ; m4 =  4,  5, 10, 11, 16, 17, 22, 23
                                 ; blend dwords
    shufps     m1, m4, m0, q3120 ; m1 =  4,  5, 16, 17,  6,  7, 18, 19
    shufps         m0, m2, q2020 ; m0 =  0,  1, 12, 13,  2,  3, 14, 15
    shufps         m2, m4, q3131 ; m2 =  8,  9, 20, 21, 10, 11, 22, 23
%if cpuflag(ssse3)
    pshufb     m3, m0, unpack_odd   ; m3 =  12,     13,     14,     15
    pshufb         m0, unpack_even  ; m0 =   0,      1,      2,      3
    pshufb     m4, m1, unpack_odd   ; m4 =  16,     17,     18,     19
    pshufb         m1, unpack_even  ; m1 =   4,      5,      6,      7
    pshufb     m5, m2, unpack_odd   ; m5 =  20,     21,     22,     23
    pshufb         m2, unpack_even  ; m2 =   8,      9,     10,     11
%else
                                 ; shuffle dwords
    pshufd     m0, m0, q3120     ; m0 =  0,  1,  2,  3, 12, 13, 14, 15
    pshufd     m1, m1, q3120     ; m1 =  4,  5,  6,  7, 16, 17, 18, 19
    pshufd     m2, m2, q3120     ; m2 =  8,  9, 10, 11, 20, 21, 22, 23
    pxor       m6, m6            ; convert s16 in m0-m2 to s32 in m0-m5
    punpcklwd  m3, m6, m0        ; m3 =      0,      1,      2,      3
    punpckhwd  m4, m6, m0        ; m4 =     12,     13,     14,     15
    punpcklwd  m0, m6, m1        ; m0 =      4,      5,      6,      7
    punpckhwd  m5, m6, m1        ; m5 =     16,     17,     18,     19
    punpcklwd  m1, m6, m2        ; m1 =      8,      9,     10,     11
    punpckhwd      m6, m2        ; m6 =     20,     21,     22,     23
    SWAP 6,2,1,0,3,4,5           ; swap registers 3,0,1,4,5,6 to 0,1,2,3,4,5
%endif
    cvtdq2ps   m0, m0            ; convert s32 to float
    cvtdq2ps   m1, m1
    cvtdq2ps   m2, m2
    cvtdq2ps   m3, m3
    cvtdq2ps   m4, m4
    cvtdq2ps   m5, m5
    mulps      m0, m7            ; scale float from s32 range to [-1.0,1.0]
    mulps      m1, m7
    mulps      m2, m7
    mulps      m3, m7
    mulps      m4, m7
    mulps      m5, m7
    mova  [dstq         ], m0
    mova  [dstq+  mmsize], m1
    mova  [dstq+2*mmsize], m2
    mova  [dstq+3*mmsize], m3
    mova  [dstq+4*mmsize], m4
    mova  [dstq+5*mmsize], m5
    add      srcq, mmsize/2
    add      dstq, mmsize*6
    sub      lend, mmsize/4
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_S16P_TO_FLT_6CH
INIT_XMM ssse3
CONV_S16P_TO_FLT_6CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_S16P_TO_FLT_6CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_fltp_to_s16_2ch(int16_t *dst, float *const *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_FLTP_TO_S16_2CH 0
cglobal conv_fltp_to_s16_2ch, 3,4,3, dst, src0, len, src1
    lea      lenq, [4*lend]
    mov     src1q, [src0q+gprsize]
    mov     src0q, [src0q        ]
    add      dstq, lenq
    add     src0q, lenq
    add     src1q, lenq
    neg      lenq
    mova       m2, [pf_s16_scale]
%if cpuflag(ssse3)
    mova       m3, [pb_interleave_words]
%endif
.loop:
    mulps      m0, m2, [src0q+lenq] ; m0 =    0,    2,    4,    6
    mulps      m1, m2, [src1q+lenq] ; m1 =    1,    3,    5,    7
    cvtps2dq   m0, m0
    cvtps2dq   m1, m1
%if cpuflag(ssse3)
    packssdw   m0, m1               ; m0 = 0, 2, 4, 6, 1, 3, 5, 7
    pshufb     m0, m3               ; m0 = 0, 1, 2, 3, 4, 5, 6, 7
%else
    packssdw   m0, m0               ; m0 = 0, 2, 4, 6, x, x, x, x
    packssdw   m1, m1               ; m1 = 1, 3, 5, 7, x, x, x, x
    punpcklwd  m0, m1               ; m0 = 0, 1, 2, 3, 4, 5, 6, 7
%endif
    mova  [dstq+lenq], m0
    add      lenq, mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_FLTP_TO_S16_2CH
INIT_XMM ssse3
CONV_FLTP_TO_S16_2CH

;------------------------------------------------------------------------------
; void ff_conv_fltp_to_s16_6ch(int16_t *dst, float *const *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_FLTP_TO_S16_6CH 0
%if ARCH_X86_64
cglobal conv_fltp_to_s16_6ch, 3,8,7, dst, src, len, src1, src2, src3, src4, src5
%else
cglobal conv_fltp_to_s16_6ch, 2,7,7, dst, src, src1, src2, src3, src4, src5
%define lend dword r2m
%endif
    mov        src1q, [srcq+1*gprsize]
    mov        src2q, [srcq+2*gprsize]
    mov        src3q, [srcq+3*gprsize]
    mov        src4q, [srcq+4*gprsize]
    mov        src5q, [srcq+5*gprsize]
    mov         srcq, [srcq]
    sub        src1q, srcq
    sub        src2q, srcq
    sub        src3q, srcq
    sub        src4q, srcq
    sub        src5q, srcq
    movaps      xmm6, [pf_s16_scale]
.loop:
%if cpuflag(sse2)
    mulps         m0, m6, [srcq      ]
    mulps         m1, m6, [srcq+src1q]
    mulps         m2, m6, [srcq+src2q]
    mulps         m3, m6, [srcq+src3q]
    mulps         m4, m6, [srcq+src4q]
    mulps         m5, m6, [srcq+src5q]
    cvtps2dq      m0, m0
    cvtps2dq      m1, m1
    cvtps2dq      m2, m2
    cvtps2dq      m3, m3
    cvtps2dq      m4, m4
    cvtps2dq      m5, m5
    packssdw      m0, m3            ; m0 =  0,  6, 12, 18,  3,  9, 15, 21
    packssdw      m1, m4            ; m1 =  1,  7, 13, 19,  4, 10, 16, 22
    packssdw      m2, m5            ; m2 =  2,  8, 14, 20,  5, 11, 17, 23
                                    ; unpack words:
    movhlps       m3, m0            ; m3 =  3,  9, 15, 21,  x,  x,  x,  x
    punpcklwd     m0, m1            ; m0 =  0,  1,  6,  7, 12, 13, 18, 19
    punpckhwd     m1, m2            ; m1 =  4,  5, 10, 11, 16, 17, 22, 23
    punpcklwd     m2, m3            ; m2 =  2,  3,  8,  9, 14, 15, 20, 21
                                    ; blend dwords:
    shufps        m3, m0, m2, q2020 ; m3 =  0,  1, 12, 13,  2,  3, 14, 15
    shufps        m0, m1, q2031     ; m0 =  6,  7, 18, 19,  4,  5, 16, 17
    shufps        m2, m1, q3131     ; m2 =  8,  9, 20, 21, 10, 11, 22, 23
                                    ; shuffle dwords:
    shufps        m1, m2, m3, q3120 ; m1 =  8,  9, 10, 11, 12, 13, 14, 15
    shufps        m3, m0,     q0220 ; m3 =  0,  1,  2,  3,  4,  5,  6,  7
    shufps        m0, m2,     q3113 ; m0 = 16, 17, 18, 19, 20, 21, 22, 23
    mova  [dstq+0*mmsize], m3
    mova  [dstq+1*mmsize], m1
    mova  [dstq+2*mmsize], m0
%else ; sse
    movlps      xmm0, [srcq      ]
    movlps      xmm1, [srcq+src1q]
    movlps      xmm2, [srcq+src2q]
    movlps      xmm3, [srcq+src3q]
    movlps      xmm4, [srcq+src4q]
    movlps      xmm5, [srcq+src5q]
    mulps       xmm0, xmm6
    mulps       xmm1, xmm6
    mulps       xmm2, xmm6
    mulps       xmm3, xmm6
    mulps       xmm4, xmm6
    mulps       xmm5, xmm6
    cvtps2pi     mm0, xmm0
    cvtps2pi     mm1, xmm1
    cvtps2pi     mm2, xmm2
    cvtps2pi     mm3, xmm3
    cvtps2pi     mm4, xmm4
    cvtps2pi     mm5, xmm5
    packssdw     mm0, mm3           ; m0 =  0,  6,  3,  9
    packssdw     mm1, mm4           ; m1 =  1,  7,  4, 10
    packssdw     mm2, mm5           ; m2 =  2,  8,  5, 11
                                    ; unpack words
    pshufw       mm3, mm0, q1032    ; m3 =  3,  9,  0,  6
    punpcklwd    mm0, mm1           ; m0 =  0,  1,  6,  7
    punpckhwd    mm1, mm2           ; m1 =  4,  5, 10, 11
    punpcklwd    mm2, mm3           ; m2 =  2,  3,  8,  9
                                    ; unpack dwords
    pshufw       mm3, mm0, q1032    ; m3 =  6,  7,  0,  1
    punpckldq    mm0, mm2           ; m0 =  0,  1,  2,  3 (final)
    punpckhdq    mm2, mm1           ; m2 =  8,  9, 10, 11 (final)
    punpckldq    mm1, mm3           ; m1 =  4,  5,  6,  7 (final)
    mova  [dstq+0*mmsize], mm0
    mova  [dstq+1*mmsize], mm1
    mova  [dstq+2*mmsize], mm2
%endif
    add       srcq, mmsize
    add       dstq, mmsize*3
    sub       lend, mmsize/4
    jg .loop
%if mmsize == 8
    emms
    RET
%else
    REP_RET
%endif
%endmacro

INIT_MMX sse
CONV_FLTP_TO_S16_6CH
INIT_XMM sse2
CONV_FLTP_TO_S16_6CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_FLTP_TO_S16_6CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_fltp_to_flt_2ch(float *dst, float *const *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_FLTP_TO_FLT_2CH 0
cglobal conv_fltp_to_flt_2ch, 3,4,5, dst, src0, len, src1
    mov  src1q, [src0q+gprsize]
    mov  src0q, [src0q]
    lea   lenq, [4*lend]
    add  src0q, lenq
    add  src1q, lenq
    lea   dstq, [dstq+2*lenq]
    neg   lenq
.loop:
    mova    m0, [src0q+lenq       ]
    mova    m1, [src1q+lenq       ]
    mova    m2, [src0q+lenq+mmsize]
    mova    m3, [src1q+lenq+mmsize]
    SBUTTERFLYPS 0, 1, 4
    SBUTTERFLYPS 2, 3, 4
    mova  [dstq+2*lenq+0*mmsize], m0
    mova  [dstq+2*lenq+1*mmsize], m1
    mova  [dstq+2*lenq+2*mmsize], m2
    mova  [dstq+2*lenq+3*mmsize], m3
    add   lenq, 2*mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse
CONV_FLTP_TO_FLT_2CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_FLTP_TO_FLT_2CH
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
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_FLTP_TO_FLT_6CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_s16_to_s16p_2ch(int16_t *const *dst, int16_t *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_S16_TO_S16P_2CH 0
cglobal conv_s16_to_s16p_2ch, 3,4,4, dst0, src, len, dst1
    lea       lenq, [2*lend]
    mov      dst1q, [dst0q+gprsize]
    mov      dst0q, [dst0q        ]
    lea       srcq, [srcq+2*lenq]
    add      dst0q, lenq
    add      dst1q, lenq
    neg       lenq
%if cpuflag(ssse3)
    mova        m3, [pb_deinterleave_words]
%endif
.loop:
    mova        m0, [srcq+2*lenq       ]  ; m0 =  0,  1,  2,  3,  4,  5,  6,  7
    mova        m1, [srcq+2*lenq+mmsize]  ; m1 =  8,  9, 10, 11, 12, 13, 14, 15
%if cpuflag(ssse3)
    pshufb      m0, m3                    ; m0 =  0,  2,  4,  6,  1,  3,  5,  7
    pshufb      m1, m3                    ; m1 =  8, 10, 12, 14,  9, 11, 13, 15
    SBUTTERFLY2 qdq, 0, 1, 2              ; m0 =  0,  2,  4,  6,  8, 10, 12, 14
                                          ; m1 =  1,  3,  5,  7,  9, 11, 13, 15
%else ; sse2
    pshuflw     m0, m0, q3120             ; m0 =  0,  2,  1,  3,  4,  5,  6,  7
    pshufhw     m0, m0, q3120             ; m0 =  0,  2,  1,  3,  4,  6,  5,  7
    pshuflw     m1, m1, q3120             ; m1 =  8, 10,  9, 11, 12, 13, 14, 15
    pshufhw     m1, m1, q3120             ; m1 =  8, 10,  9, 11, 12, 14, 13, 15
    DEINT2_PS    0, 1, 2                  ; m0 =  0,  2,  4,  6,  8, 10, 12, 14
                                          ; m1 =  1,  3,  5,  7,  9, 11, 13, 15
%endif
    mova  [dst0q+lenq], m0
    mova  [dst1q+lenq], m1
    add       lenq, mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_S16_TO_S16P_2CH
INIT_XMM ssse3
CONV_S16_TO_S16P_2CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_S16_TO_S16P_2CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_s16_to_s16p_6ch(int16_t *const *dst, int16_t *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_S16_TO_S16P_6CH 0
%if ARCH_X86_64
cglobal conv_s16_to_s16p_6ch, 3,8,5, dst, src, len, dst1, dst2, dst3, dst4, dst5
%else
cglobal conv_s16_to_s16p_6ch, 2,7,5, dst, src, dst1, dst2, dst3, dst4, dst5
%define lend dword r2m
%endif
    mov     dst1q, [dstq+  gprsize]
    mov     dst2q, [dstq+2*gprsize]
    mov     dst3q, [dstq+3*gprsize]
    mov     dst4q, [dstq+4*gprsize]
    mov     dst5q, [dstq+5*gprsize]
    mov      dstq, [dstq          ]
    sub     dst1q, dstq
    sub     dst2q, dstq
    sub     dst3q, dstq
    sub     dst4q, dstq
    sub     dst5q, dstq
.loop:
    mova       m0, [srcq+0*mmsize]      ; m0 =  0,  1,  2,  3,  4,  5,  6,  7
    mova       m3, [srcq+1*mmsize]      ; m3 =  8,  9, 10, 11, 12, 13, 14, 15
    mova       m2, [srcq+2*mmsize]      ; m2 = 16, 17, 18, 19, 20, 21, 22, 23
    PALIGNR    m1, m3, m0, 12, m4       ; m1 =  6,  7,  8,  9, 10, 11,  x,  x
    shufps     m3, m2, q1032            ; m3 = 12, 13, 14, 15, 16, 17, 18, 19
    psrldq     m2, 4                    ; m2 = 18, 19, 20, 21, 22, 23,  x,  x
    SBUTTERFLY2 wd, 0, 1, 4             ; m0 =  0,  6,  1,  7,  2,  8,  3,  9
                                        ; m1 =  4, 10,  5, 11,  x,  x,  x,  x
    SBUTTERFLY2 wd, 3, 2, 4             ; m3 = 12, 18, 13, 19, 14, 20, 15, 21
                                        ; m2 = 16, 22, 17, 23,  x,  x,  x,  x
    SBUTTERFLY2 dq, 0, 3, 4             ; m0 =  0,  6, 12, 18,  1,  7, 13, 19
                                        ; m3 =  2,  8, 14, 20,  3,  9, 15, 21
    punpckldq  m1, m2                   ; m1 =  4, 10, 16, 22,  5, 11, 17, 23
    movq    [dstq      ], m0
    movhps  [dstq+dst1q], m0
    movq    [dstq+dst2q], m3
    movhps  [dstq+dst3q], m3
    movq    [dstq+dst4q], m1
    movhps  [dstq+dst5q], m1
    add      srcq, mmsize*3
    add      dstq, mmsize/2
    sub      lend, mmsize/4
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_S16_TO_S16P_6CH
INIT_XMM ssse3
CONV_S16_TO_S16P_6CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_S16_TO_S16P_6CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_s16_to_fltp_2ch(float *const *dst, int16_t *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_S16_TO_FLTP_2CH 0
cglobal conv_s16_to_fltp_2ch, 3,4,5, dst0, src, len, dst1
    lea       lenq, [4*lend]
    mov      dst1q, [dst0q+gprsize]
    mov      dst0q, [dst0q        ]
    add       srcq, lenq
    add      dst0q, lenq
    add      dst1q, lenq
    neg       lenq
    mova        m3, [pf_s32_inv_scale]
    mova        m4, [pw_zero_even]
.loop:
    mova        m1, [srcq+lenq]
    pslld       m0, m1, 16
    pand        m1, m4
    cvtdq2ps    m0, m0
    cvtdq2ps    m1, m1
    mulps       m0, m0, m3
    mulps       m1, m1, m3
    mova  [dst0q+lenq], m0
    mova  [dst1q+lenq], m1
    add       lenq, mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_S16_TO_FLTP_2CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_S16_TO_FLTP_2CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_s16_to_fltp_6ch(float *const *dst, int16_t *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_S16_TO_FLTP_6CH 0
%if ARCH_X86_64
cglobal conv_s16_to_fltp_6ch, 3,8,7, dst, src, len, dst1, dst2, dst3, dst4, dst5
%else
cglobal conv_s16_to_fltp_6ch, 2,7,7, dst, src, dst1, dst2, dst3, dst4, dst5
%define lend dword r2m
%endif
    mov     dst1q, [dstq+  gprsize]
    mov     dst2q, [dstq+2*gprsize]
    mov     dst3q, [dstq+3*gprsize]
    mov     dst4q, [dstq+4*gprsize]
    mov     dst5q, [dstq+5*gprsize]
    mov      dstq, [dstq          ]
    sub     dst1q, dstq
    sub     dst2q, dstq
    sub     dst3q, dstq
    sub     dst4q, dstq
    sub     dst5q, dstq
    mova       m6, [pf_s16_inv_scale]
.loop:
    mova       m0, [srcq+0*mmsize]  ; m0 =  0,  1,  2,  3,  4,  5,  6,  7
    mova       m3, [srcq+1*mmsize]  ; m3 =  8,  9, 10, 11, 12, 13, 14, 15
    mova       m2, [srcq+2*mmsize]  ; m2 = 16, 17, 18, 19, 20, 21, 22, 23
    PALIGNR    m1, m3, m0, 12, m4   ; m1 =  6,  7,  8,  9, 10, 11,  x,  x
    shufps     m3, m2, q1032        ; m3 = 12, 13, 14, 15, 16, 17, 18, 19
    psrldq     m2, 4                ; m2 = 18, 19, 20, 21, 22, 23,  x,  x
    SBUTTERFLY2 wd, 0, 1, 4         ; m0 =  0,  6,  1,  7,  2,  8,  3,  9
                                    ; m1 =  4, 10,  5, 11,  x,  x,  x,  x
    SBUTTERFLY2 wd, 3, 2, 4         ; m3 = 12, 18, 13, 19, 14, 20, 15, 21
                                    ; m2 = 16, 22, 17, 23,  x,  x,  x,  x
    SBUTTERFLY2 dq, 0, 3, 4         ; m0 =  0,  6, 12, 18,  1,  7, 13, 19
                                    ; m3 =  2,  8, 14, 20,  3,  9, 15, 21
    punpckldq  m1, m2               ; m1 =  4, 10, 16, 22,  5, 11, 17, 23
    S16_TO_S32_SX 0, 2              ; m0 =      0,      6,     12,     18
                                    ; m2 =      1,      7,     13,     19
    S16_TO_S32_SX 3, 4              ; m3 =      2,      8,     14,     20
                                    ; m4 =      3,      9,     15,     21
    S16_TO_S32_SX 1, 5              ; m1 =      4,     10,     16,     22
                                    ; m5 =      5,     11,     17,     23
    SWAP 1,2,3,4
    cvtdq2ps   m0, m0
    cvtdq2ps   m1, m1
    cvtdq2ps   m2, m2
    cvtdq2ps   m3, m3
    cvtdq2ps   m4, m4
    cvtdq2ps   m5, m5
    mulps      m0, m6
    mulps      m1, m6
    mulps      m2, m6
    mulps      m3, m6
    mulps      m4, m6
    mulps      m5, m6
    mova  [dstq      ], m0
    mova  [dstq+dst1q], m1
    mova  [dstq+dst2q], m2
    mova  [dstq+dst3q], m3
    mova  [dstq+dst4q], m4
    mova  [dstq+dst5q], m5
    add      srcq, mmsize*3
    add      dstq, mmsize
    sub      lend, mmsize/4
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_S16_TO_FLTP_6CH
INIT_XMM ssse3
CONV_S16_TO_FLTP_6CH
INIT_XMM sse4
CONV_S16_TO_FLTP_6CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_S16_TO_FLTP_6CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_flt_to_s16p_2ch(int16_t *const *dst, float *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_FLT_TO_S16P_2CH 0
cglobal conv_flt_to_s16p_2ch, 3,4,6, dst0, src, len, dst1
    lea       lenq, [2*lend]
    mov      dst1q, [dst0q+gprsize]
    mov      dst0q, [dst0q        ]
    lea       srcq, [srcq+4*lenq]
    add      dst0q, lenq
    add      dst1q, lenq
    neg       lenq
    mova        m5, [pf_s16_scale]
.loop:
    mova       m0, [srcq+4*lenq         ]
    mova       m1, [srcq+4*lenq+  mmsize]
    mova       m2, [srcq+4*lenq+2*mmsize]
    mova       m3, [srcq+4*lenq+3*mmsize]
    DEINT2_PS   0, 1, 4
    DEINT2_PS   2, 3, 4
    mulps      m0, m0, m5
    mulps      m1, m1, m5
    mulps      m2, m2, m5
    mulps      m3, m3, m5
    cvtps2dq   m0, m0
    cvtps2dq   m1, m1
    cvtps2dq   m2, m2
    cvtps2dq   m3, m3
    packssdw   m0, m2
    packssdw   m1, m3
    mova  [dst0q+lenq], m0
    mova  [dst1q+lenq], m1
    add      lenq, mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_FLT_TO_S16P_2CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_FLT_TO_S16P_2CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_flt_to_s16p_6ch(int16_t *const *dst, float *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_FLT_TO_S16P_6CH 0
%if ARCH_X86_64
cglobal conv_flt_to_s16p_6ch, 3,8,7, dst, src, len, dst1, dst2, dst3, dst4, dst5
%else
cglobal conv_flt_to_s16p_6ch, 2,7,7, dst, src, dst1, dst2, dst3, dst4, dst5
%define lend dword r2m
%endif
    mov     dst1q, [dstq+  gprsize]
    mov     dst2q, [dstq+2*gprsize]
    mov     dst3q, [dstq+3*gprsize]
    mov     dst4q, [dstq+4*gprsize]
    mov     dst5q, [dstq+5*gprsize]
    mov      dstq, [dstq          ]
    sub     dst1q, dstq
    sub     dst2q, dstq
    sub     dst3q, dstq
    sub     dst4q, dstq
    sub     dst5q, dstq
    mova       m6, [pf_s16_scale]
.loop:
    mulps      m0, m6, [srcq+0*mmsize]
    mulps      m3, m6, [srcq+1*mmsize]
    mulps      m1, m6, [srcq+2*mmsize]
    mulps      m4, m6, [srcq+3*mmsize]
    mulps      m2, m6, [srcq+4*mmsize]
    mulps      m5, m6, [srcq+5*mmsize]
    cvtps2dq   m0, m0
    cvtps2dq   m1, m1
    cvtps2dq   m2, m2
    cvtps2dq   m3, m3
    cvtps2dq   m4, m4
    cvtps2dq   m5, m5
    packssdw   m0, m3               ; m0 =  0,  1,  2,  3,  4,  5,  6,  7
    packssdw   m1, m4               ; m1 =  8,  9, 10, 11, 12, 13, 14, 15
    packssdw   m2, m5               ; m2 = 16, 17, 18, 19, 20, 21, 22, 23
    PALIGNR    m3, m1, m0, 12, m4   ; m3 =  6,  7,  8,  9, 10, 11,  x,  x
    shufps     m1, m2, q1032        ; m1 = 12, 13, 14, 15, 16, 17, 18, 19
    psrldq     m2, 4                ; m2 = 18, 19, 20, 21, 22, 23,  x,  x
    SBUTTERFLY2 wd, 0, 3, 4         ; m0 =  0,  6,  1,  7,  2,  8,  3,  9
                                    ; m3 =  4, 10,  5, 11,  x,  x,  x,  x
    SBUTTERFLY2 wd, 1, 2, 4         ; m1 = 12, 18, 13, 19, 14, 20, 15, 21
                                    ; m2 = 16, 22, 17, 23,  x,  x,  x,  x
    SBUTTERFLY2 dq, 0, 1, 4         ; m0 =  0,  6, 12, 18,  1,  7, 13, 19
                                    ; m1 =  2,  8, 14, 20,  3,  9, 15, 21
    punpckldq  m3, m2               ; m3 =  4, 10, 16, 22,  5, 11, 17, 23
    movq    [dstq      ], m0
    movhps  [dstq+dst1q], m0
    movq    [dstq+dst2q], m1
    movhps  [dstq+dst3q], m1
    movq    [dstq+dst4q], m3
    movhps  [dstq+dst5q], m3
    add      srcq, mmsize*6
    add      dstq, mmsize/2
    sub      lend, mmsize/4
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_FLT_TO_S16P_6CH
INIT_XMM ssse3
CONV_FLT_TO_S16P_6CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_FLT_TO_S16P_6CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_flt_to_fltp_2ch(float *const *dst, float *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_FLT_TO_FLTP_2CH 0
cglobal conv_flt_to_fltp_2ch, 3,4,3, dst0, src, len, dst1
    lea    lenq, [4*lend]
    mov   dst1q, [dst0q+gprsize]
    mov   dst0q, [dst0q        ]
    lea    srcq, [srcq+2*lenq]
    add   dst0q, lenq
    add   dst1q, lenq
    neg    lenq
.loop:
    mova     m0, [srcq+2*lenq       ]
    mova     m1, [srcq+2*lenq+mmsize]
    DEINT2_PS 0, 1, 2
    mova  [dst0q+lenq], m0
    mova  [dst1q+lenq], m1
    add    lenq, mmsize
    jl .loop
    REP_RET
%endmacro

INIT_XMM sse
CONV_FLT_TO_FLTP_2CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_FLT_TO_FLTP_2CH
%endif

;------------------------------------------------------------------------------
; void ff_conv_flt_to_fltp_6ch(float *const *dst, float *src, int len,
;                              int channels);
;------------------------------------------------------------------------------

%macro CONV_FLT_TO_FLTP_6CH 0
%if ARCH_X86_64
cglobal conv_flt_to_fltp_6ch, 3,8,7, dst, src, len, dst1, dst2, dst3, dst4, dst5
%else
cglobal conv_flt_to_fltp_6ch, 2,7,7, dst, src, dst1, dst2, dst3, dst4, dst5
%define lend dword r2m
%endif
    mov     dst1q, [dstq+  gprsize]
    mov     dst2q, [dstq+2*gprsize]
    mov     dst3q, [dstq+3*gprsize]
    mov     dst4q, [dstq+4*gprsize]
    mov     dst5q, [dstq+5*gprsize]
    mov      dstq, [dstq          ]
    sub     dst1q, dstq
    sub     dst2q, dstq
    sub     dst3q, dstq
    sub     dst4q, dstq
    sub     dst5q, dstq
.loop:
    mova       m0, [srcq+0*mmsize]  ; m0 =  0,  1,  2,  3
    mova       m1, [srcq+1*mmsize]  ; m1 =  4,  5,  6,  7
    mova       m2, [srcq+2*mmsize]  ; m2 =  8,  9, 10, 11
    mova       m3, [srcq+3*mmsize]  ; m3 = 12, 13, 14, 15
    mova       m4, [srcq+4*mmsize]  ; m4 = 16, 17, 18, 19
    mova       m5, [srcq+5*mmsize]  ; m5 = 20, 21, 22, 23

    SBUTTERFLY2 dq, 0, 3, 6         ; m0 =  0, 12,  1, 13
                                    ; m3 =  2, 14,  3, 15
    SBUTTERFLY2 dq, 1, 4, 6         ; m1 =  4, 16,  5, 17
                                    ; m4 =  6, 18,  7, 19
    SBUTTERFLY2 dq, 2, 5, 6         ; m2 =  8, 20,  9, 21
                                    ; m5 = 10, 22, 11, 23
    SBUTTERFLY2 dq, 0, 4, 6         ; m0 =  0,  6, 12, 18
                                    ; m4 =  1,  7, 13, 19
    SBUTTERFLY2 dq, 3, 2, 6         ; m3 =  2,  8, 14, 20
                                    ; m2 =  3,  9, 15, 21
    SBUTTERFLY2 dq, 1, 5, 6         ; m1 =  4, 10, 16, 22
                                    ; m5 =  5, 11, 17, 23
    mova [dstq      ], m0
    mova [dstq+dst1q], m4
    mova [dstq+dst2q], m3
    mova [dstq+dst3q], m2
    mova [dstq+dst4q], m1
    mova [dstq+dst5q], m5
    add      srcq, mmsize*6
    add      dstq, mmsize
    sub      lend, mmsize/4
    jg .loop
    REP_RET
%endmacro

INIT_XMM sse2
CONV_FLT_TO_FLTP_6CH
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CONV_FLT_TO_FLTP_6CH
%endif
