;******************************************************************************
;* Copyright (c) 2012 Michael Niedermayer
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

SECTION_RODATA 32
flt2pm31: times 8 dd 4.6566129e-10
flt2p31 : times 8 dd 2147483648.0
flt2p15 : times 8 dd 32768.0

word_unpack_shuf : db  0, 1, 4, 5, 8, 9,12,13, 2, 3, 6, 7,10,11,14,15

SECTION .text


;to, from, a/u, log2_outsize, log_intsize, const
%macro PACK_2CH 5-7
cglobal pack_2ch_%2_to_%1_%3, 3, 4, 6, dst, src, len, src2
    mov src2q   , [srcq+gprsize]
    mov srcq    , [srcq]
    mov dstq    , [dstq]
%ifidn %3, a
    test dstq, mmsize-1
        jne pack_2ch_%2_to_%1_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne pack_2ch_%2_to_%1_u_int %+ SUFFIX
    test src2q, mmsize-1
        jne pack_2ch_%2_to_%1_u_int %+ SUFFIX
%else
pack_2ch_%2_to_%1_u_int %+ SUFFIX:
%endif
    lea     srcq , [srcq  + (1<<%5)*lenq]
    lea     src2q, [src2q + (1<<%5)*lenq]
    lea     dstq , [dstq  + (2<<%4)*lenq]
    neg     lenq
    %7 m0,m1,m2,m3,m4,m5
.next:
%if %4 >= %5
    mov%3     m0, [         srcq +(1<<%5)*lenq]
    mova      m1, m0
    mov%3     m2, [         src2q+(1<<%5)*lenq]
%if %5 == 1
    punpcklwd m0, m2
    punpckhwd m1, m2
%else
    punpckldq m0, m2
    punpckhdq m1, m2
%endif
    %6 m0,m1,m2,m3,m4,m5
%else
    mov%3     m0, [         srcq +(1<<%5)*lenq]
    mov%3     m1, [mmsize + srcq +(1<<%5)*lenq]
    mov%3     m2, [         src2q+(1<<%5)*lenq]
    mov%3     m3, [mmsize + src2q+(1<<%5)*lenq]
    %6 m0,m1,m2,m3,m4,m5
    mova      m2, m0
    punpcklwd m0, m1
    punpckhwd m2, m1
    SWAP 1,2
%endif
    mov%3 [           dstq+(2<<%4)*lenq], m0
    mov%3 [  mmsize + dstq+(2<<%4)*lenq], m1
%if %4 > %5
    mov%3 [2*mmsize + dstq+(2<<%4)*lenq], m2
    mov%3 [3*mmsize + dstq+(2<<%4)*lenq], m3
    add lenq, 4*mmsize/(2<<%4)
%else
    add lenq, 2*mmsize/(2<<%4)
%endif
        jl .next
    REP_RET
%endmacro

%macro UNPACK_2CH 5-7
cglobal unpack_2ch_%2_to_%1_%3, 3, 4, 7, dst, src, len, dst2
    mov dst2q   , [dstq+gprsize]
    mov srcq    , [srcq]
    mov dstq    , [dstq]
%ifidn %3, a
    test dstq, mmsize-1
        jne unpack_2ch_%2_to_%1_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne unpack_2ch_%2_to_%1_u_int %+ SUFFIX
    test dst2q, mmsize-1
        jne unpack_2ch_%2_to_%1_u_int %+ SUFFIX
%else
unpack_2ch_%2_to_%1_u_int %+ SUFFIX:
%endif
    lea     srcq , [srcq  + (2<<%5)*lenq]
    lea     dstq , [dstq  + (1<<%4)*lenq]
    lea     dst2q, [dst2q + (1<<%4)*lenq]
    neg     lenq
    %7 m0,m1,m2,m3,m4,m5
    mova      m6, [word_unpack_shuf]
.next:
    mov%3     m0, [           srcq +(2<<%5)*lenq]
    mov%3     m2, [  mmsize + srcq +(2<<%5)*lenq]
%if %5 == 1
%ifidn SUFFIX, _ssse3
    pshufb    m0, m6
    mova      m1, m0
    pshufb    m2, m6
    punpcklqdq m0,m2
    punpckhqdq m1,m2
%else
    mova      m1, m0
    punpcklwd m0,m2
    punpckhwd m1,m2

    mova      m2, m0
    punpcklwd m0,m1
    punpckhwd m2,m1

    mova      m1, m0
    punpcklwd m0,m2
    punpckhwd m1,m2
%endif
%else
    mova      m1, m0
    shufps    m0, m2, 10001000b
    shufps    m1, m2, 11011101b
%endif
%if %4 < %5
    mov%3     m2, [2*mmsize + srcq +(2<<%5)*lenq]
    mova      m3, m2
    mov%3     m4, [3*mmsize + srcq +(2<<%5)*lenq]
    shufps    m2, m4, 10001000b
    shufps    m3, m4, 11011101b
    SWAP 1,2
%endif
    %6 m0,m1,m2,m3,m4,m5
    mov%3 [           dstq+(1<<%4)*lenq], m0
%if %4 > %5
    mov%3 [          dst2q+(1<<%4)*lenq], m2
    mov%3 [ mmsize +  dstq+(1<<%4)*lenq], m1
    mov%3 [ mmsize + dst2q+(1<<%4)*lenq], m3
    add lenq, 2*mmsize/(1<<%4)
%else
    mov%3 [          dst2q+(1<<%4)*lenq], m1
    add lenq, mmsize/(1<<%4)
%endif
        jl .next
    REP_RET
%endmacro

%macro CONV 5-7
cglobal %2_to_%1_%3, 3, 3, 6, dst, src, len
    mov srcq    , [srcq]
    mov dstq    , [dstq]
%ifidn %3, a
    test dstq, mmsize-1
        jne %2_to_%1_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne %2_to_%1_u_int %+ SUFFIX
%else
%2_to_%1_u_int %+ SUFFIX:
%endif
    lea     srcq , [srcq  + (1<<%5)*lenq]
    lea     dstq , [dstq  + (1<<%4)*lenq]
    neg     lenq
    %7 m0,m1,m2,m3,m4,m5
.next:
    mov%3     m0, [           srcq +(1<<%5)*lenq]
    mov%3     m1, [  mmsize + srcq +(1<<%5)*lenq]
%if %4 < %5
    mov%3     m2, [2*mmsize + srcq +(1<<%5)*lenq]
    mov%3     m3, [3*mmsize + srcq +(1<<%5)*lenq]
%endif
    %6 m0,m1,m2,m3,m4,m5
    mov%3 [           dstq+(1<<%4)*lenq], m0
    mov%3 [  mmsize + dstq+(1<<%4)*lenq], m1
%if %4 > %5
    mov%3 [2*mmsize + dstq+(1<<%4)*lenq], m2
    mov%3 [3*mmsize + dstq+(1<<%4)*lenq], m3
    add lenq, 4*mmsize/(1<<%4)
%else
    add lenq, 2*mmsize/(1<<%4)
%endif
        jl .next
%if mmsize == 8
    emms
    RET
%else
    REP_RET
%endif
%endmacro

%macro PACK_6CH 8
cglobal pack_6ch_%2_to_%1_%3, 2, 8, %6, dst, src, src1, src2, src3, src4, src5, len
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
    mov     dstq, [dstq]
%ifidn %3, a
    test dstq, mmsize-1
        jne pack_6ch_%2_to_%1_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne pack_6ch_%2_to_%1_u_int %+ SUFFIX
    test src1q, mmsize-1
        jne pack_6ch_%2_to_%1_u_int %+ SUFFIX
    test src2q, mmsize-1
        jne pack_6ch_%2_to_%1_u_int %+ SUFFIX
    test src3q, mmsize-1
        jne pack_6ch_%2_to_%1_u_int %+ SUFFIX
    test src4q, mmsize-1
        jne pack_6ch_%2_to_%1_u_int %+ SUFFIX
    test src5q, mmsize-1
        jne pack_6ch_%2_to_%1_u_int %+ SUFFIX
%else
pack_6ch_%2_to_%1_u_int %+ SUFFIX:
%endif
    sub    src1q, srcq
    sub    src2q, srcq
    sub    src3q, srcq
    sub    src4q, srcq
    sub    src5q, srcq
    %8 x,x,x,x,m7,x
.loop:
    mov%3     m0, [srcq      ]
    mov%3     m1, [srcq+src1q]
    mov%3     m2, [srcq+src2q]
    mov%3     m3, [srcq+src3q]
    mov%3     m4, [srcq+src4q]
    mov%3     m5, [srcq+src5q]
%if cpuflag(sse)
    SBUTTERFLYPS 0, 1, 6
    SBUTTERFLYPS 2, 3, 6
    SBUTTERFLYPS 4, 5, 6

%if cpuflag(avx)
    blendps   m6, m4, m0, 1100b
%else
    movaps    m6, m4
    shufps    m4, m0, q3210
    SWAP 4,6
%endif
    movlhps   m0, m2
    movhlps   m4, m2
%if cpuflag(avx)
    blendps   m2, m5, m1, 1100b
%else
    movaps    m2, m5
    shufps    m5, m1, q3210
    SWAP 2,5
%endif
    movlhps   m1, m3
    movhlps   m5, m3

    %7 m0,m6,x,x,m7,m3
    %7 m4,m1,x,x,m7,m3
    %7 m2,m5,x,x,m7,m3

    mov %+ %3 %+ ps [dstq   ], m0
    mov %+ %3 %+ ps [dstq+16], m6
    mov %+ %3 %+ ps [dstq+32], m4
    mov %+ %3 %+ ps [dstq+48], m1
    mov %+ %3 %+ ps [dstq+64], m2
    mov %+ %3 %+ ps [dstq+80], m5
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

%macro UNPACK_6CH 8
cglobal unpack_6ch_%2_to_%1_%3, 2, 8, %6, dst, src, dst1, dst2, dst3, dst4, dst5, len
%if ARCH_X86_64
    mov     lend, r2d
%else
    %define lend dword r2m
%endif
    mov    dst1q, [dstq+1*gprsize]
    mov    dst2q, [dstq+2*gprsize]
    mov    dst3q, [dstq+3*gprsize]
    mov    dst4q, [dstq+4*gprsize]
    mov    dst5q, [dstq+5*gprsize]
    mov     dstq, [dstq]
    mov     srcq, [srcq]
%ifidn %3, a
    test dstq, mmsize-1
        jne unpack_6ch_%2_to_%1_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne unpack_6ch_%2_to_%1_u_int %+ SUFFIX
    test dst1q, mmsize-1
        jne unpack_6ch_%2_to_%1_u_int %+ SUFFIX
    test dst2q, mmsize-1
        jne unpack_6ch_%2_to_%1_u_int %+ SUFFIX
    test dst3q, mmsize-1
        jne unpack_6ch_%2_to_%1_u_int %+ SUFFIX
    test dst4q, mmsize-1
        jne unpack_6ch_%2_to_%1_u_int %+ SUFFIX
    test dst5q, mmsize-1
        jne unpack_6ch_%2_to_%1_u_int %+ SUFFIX
%else
unpack_6ch_%2_to_%1_u_int %+ SUFFIX:
%endif
    sub    dst1q, dstq
    sub    dst2q, dstq
    sub    dst3q, dstq
    sub    dst4q, dstq
    sub    dst5q, dstq
    %8 x,x,x,x,m7,x
.loop:
    mov%3     m0, [srcq   ]
    mov%3     m1, [srcq+16]
    mov%3     m2, [srcq+32]
    mov%3     m3, [srcq+48]
    mov%3     m4, [srcq+64]
    mov%3     m5, [srcq+80]

    SBUTTERFLYPS 0, 3, 6
    SBUTTERFLYPS 1, 4, 6
    SBUTTERFLYPS 2, 5, 6
    SBUTTERFLYPS 0, 4, 6
    SBUTTERFLYPS 3, 2, 6
    SBUTTERFLYPS 1, 5, 6
    SWAP 1, 4
    SWAP 2, 3

    %7 m0,m1,x,x,m7,m6
    %7 m2,m3,x,x,m7,m6
    %7 m4,m5,x,x,m7,m6

    mov %+ %3 %+ ps [dstq      ], m0
    mov %+ %3 %+ ps [dstq+dst1q], m1
    mov %+ %3 %+ ps [dstq+dst2q], m2
    mov %+ %3 %+ ps [dstq+dst3q], m3
    mov %+ %3 %+ ps [dstq+dst4q], m4
    mov %+ %3 %+ ps [dstq+dst5q], m5

    add      srcq, mmsize*6
    add      dstq, mmsize
    sub      lend, mmsize/4
    jg .loop
    REP_RET
%endmacro

%define PACK_8CH_GPRS (10 * ARCH_X86_64) + ((6 + HAVE_ALIGNED_STACK) * ARCH_X86_32)

%macro PACK_8CH 8
cglobal pack_8ch_%2_to_%1_%3, 2, PACK_8CH_GPRS, %6, ARCH_X86_32*48, dst, src, len, src1, src2, src3, src4, src5, src6, src7
    mov     dstq, [dstq]
%if ARCH_X86_32
    DEFINE_ARGS dst, src, src2, src3, src4, src5, src6
    %define lend dword r2m
    %define src1q r0q
    %define src1m dword [rsp+32]
%if HAVE_ALIGNED_STACK == 0
    DEFINE_ARGS dst, src, src2, src3, src5, src6
    %define src4q r0q
    %define src4m dword [rsp+36]
%endif
    %define src7q r0q
    %define src7m dword [rsp+40]
    mov     dstm, dstq
%endif
    mov    src7q, [srcq+7*gprsize]
    mov    src6q, [srcq+6*gprsize]
%if ARCH_X86_32
    mov    src7m, src7q
%endif
    mov    src5q, [srcq+5*gprsize]
    mov    src4q, [srcq+4*gprsize]
    mov    src3q, [srcq+3*gprsize]
%if ARCH_X86_32 && HAVE_ALIGNED_STACK == 0
    mov    src4m, src4q
%endif
    mov    src2q, [srcq+2*gprsize]
    mov    src1q, [srcq+1*gprsize]
    mov     srcq, [srcq]
%ifidn %3, a
%if ARCH_X86_32
    test dstmp, mmsize-1
%else
    test dstq, mmsize-1
%endif
        jne pack_8ch_%2_to_%1_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne pack_8ch_%2_to_%1_u_int %+ SUFFIX
    test src1q, mmsize-1
        jne pack_8ch_%2_to_%1_u_int %+ SUFFIX
    test src2q, mmsize-1
        jne pack_8ch_%2_to_%1_u_int %+ SUFFIX
    test src3q, mmsize-1
        jne pack_8ch_%2_to_%1_u_int %+ SUFFIX
%if ARCH_X86_32 && HAVE_ALIGNED_STACK == 0
    test src4m, mmsize-1
%else
    test src4q, mmsize-1
%endif
        jne pack_8ch_%2_to_%1_u_int %+ SUFFIX
    test src5q, mmsize-1
        jne pack_8ch_%2_to_%1_u_int %+ SUFFIX
    test src6q, mmsize-1
        jne pack_8ch_%2_to_%1_u_int %+ SUFFIX
%if ARCH_X86_32
    test src7m, mmsize-1
%else
    test src7q, mmsize-1
%endif
        jne pack_8ch_%2_to_%1_u_int %+ SUFFIX
%else
pack_8ch_%2_to_%1_u_int %+ SUFFIX:
%endif
    sub    src1q, srcq
    sub    src2q, srcq
    sub    src3q, srcq
%if ARCH_X86_64 || HAVE_ALIGNED_STACK
    sub    src4q, srcq
%else
    sub    src4m, srcq
%endif
    sub    src5q, srcq
    sub    src6q, srcq
%if ARCH_X86_64
    sub    src7q, srcq
%else
    mov src1m, src1q
    sub src7m, srcq
%endif

%if ARCH_X86_64
    %8 x,x,x,x,m9,x
%elifidn %1, int32
    %define m9 [flt2p31]
%else
    %define m9 [flt2pm31]
%endif

.loop:
    mov%3     m0, [srcq      ]
    mov%3     m1, [srcq+src1q]
    mov%3     m2, [srcq+src2q]
%if ARCH_X86_32 && HAVE_ALIGNED_STACK == 0
    mov    src4q, src4m
%endif
    mov%3     m3, [srcq+src3q]
    mov%3     m4, [srcq+src4q]
    mov%3     m5, [srcq+src5q]
%if ARCH_X86_32
    mov    src7q, src7m
%endif
    mov%3     m6, [srcq+src6q]
    mov%3     m7, [srcq+src7q]

%if ARCH_X86_64
    TRANSPOSE8x4D 0, 1, 2, 3, 4, 5, 6, 7, 8

    %7 m0,m1,x,x,m9,m8
    %7 m2,m3,x,x,m9,m8
    %7 m4,m5,x,x,m9,m8
    %7 m6,m7,x,x,m9,m8

    mov%3 [dstq], m0
%else
    mov     dstq, dstm

    TRANSPOSE8x4D 0, 1, 2, 3, 4, 5, 6, 7, [rsp], [rsp+16], 1

    %7 m0,m1,x,x,m9,m2
    mova     m2, [rsp]
    mov%3   [dstq], m0
    %7 m2,m3,x,x,m9,m0
    %7 m4,m5,x,x,m9,m0
    %7 m6,m7,x,x,m9,m0

%endif

    mov%3 [dstq+16],  m1
    mov%3 [dstq+32],  m2
    mov%3 [dstq+48],  m3
    mov%3 [dstq+64],  m4
    mov%3 [dstq+80],  m5
    mov%3 [dstq+96],  m6
    mov%3 [dstq+112], m7

    add      srcq, mmsize
    add      dstq, mmsize*8
%if ARCH_X86_32
    mov      dstm, dstq
    mov      src1q, src1m
%endif
    sub      lend, mmsize/4
    jg .loop
    REP_RET
%endmacro

%macro INT16_TO_INT32_N 6
    pxor      m2, m2
    pxor      m3, m3
    punpcklwd m2, m1
    punpckhwd m3, m1
    SWAP 4,0
    pxor      m0, m0
    pxor      m1, m1
    punpcklwd m0, m4
    punpckhwd m1, m4
%endmacro

%macro INT32_TO_INT16_N 6
    psrad     m0, 16
    psrad     m1, 16
    psrad     m2, 16
    psrad     m3, 16
    packssdw  m0, m1
    packssdw  m2, m3
    SWAP 1,2
%endmacro

%macro INT32_TO_FLOAT_INIT 6
    mova      %5, [flt2pm31]
%endmacro
%macro INT32_TO_FLOAT_N 6
    cvtdq2ps  %1, %1
    cvtdq2ps  %2, %2
    mulps %1, %1, %5
    mulps %2, %2, %5
%endmacro

%macro FLOAT_TO_INT32_INIT 6
    mova      %5, [flt2p31]
%endmacro
%macro FLOAT_TO_INT32_N 6
    mulps %1, %5
    mulps %2, %5
    cvtps2dq  %6, %1
    cmpps %1, %1, %5, 5
    paddd %1, %6
    cvtps2dq  %6, %2
    cmpps %2, %2, %5, 5
    paddd %2, %6
%endmacro

%macro INT16_TO_FLOAT_INIT 6
    mova      m5, [flt2pm31]
%endmacro
%macro INT16_TO_FLOAT_N 6
    INT16_TO_INT32_N %1,%2,%3,%4,%5,%6
    cvtdq2ps  m0, m0
    cvtdq2ps  m1, m1
    cvtdq2ps  m2, m2
    cvtdq2ps  m3, m3
    mulps m0, m0, m5
    mulps m1, m1, m5
    mulps m2, m2, m5
    mulps m3, m3, m5
%endmacro

%macro FLOAT_TO_INT16_INIT 6
    mova      m5, [flt2p15]
%endmacro
%macro FLOAT_TO_INT16_N 6
    mulps m0, m5
    mulps m1, m5
    mulps m2, m5
    mulps m3, m5
    cvtps2dq  m0, m0
    cvtps2dq  m1, m1
    packssdw  m0, m1
    cvtps2dq  m1, m2
    cvtps2dq  m3, m3
    packssdw  m1, m3
%endmacro

%macro NOP_N 0-6
%endmacro

INIT_XMM sse
PACK_6CH float, float, u, 2, 2, 7, NOP_N, NOP_N
PACK_6CH float, float, a, 2, 2, 7, NOP_N, NOP_N

UNPACK_6CH float, float, u, 2, 2, 7, NOP_N, NOP_N
UNPACK_6CH float, float, a, 2, 2, 7, NOP_N, NOP_N

INIT_XMM sse2
CONV int32, int16, u, 2, 1, INT16_TO_INT32_N, NOP_N
CONV int32, int16, a, 2, 1, INT16_TO_INT32_N, NOP_N
CONV int16, int32, u, 1, 2, INT32_TO_INT16_N, NOP_N
CONV int16, int32, a, 1, 2, INT32_TO_INT16_N, NOP_N

PACK_2CH int16, int16, u, 1, 1, NOP_N, NOP_N
PACK_2CH int16, int16, a, 1, 1, NOP_N, NOP_N
PACK_2CH int32, int32, u, 2, 2, NOP_N, NOP_N
PACK_2CH int32, int32, a, 2, 2, NOP_N, NOP_N
PACK_2CH int32, int16, u, 2, 1, INT16_TO_INT32_N, NOP_N
PACK_2CH int32, int16, a, 2, 1, INT16_TO_INT32_N, NOP_N
PACK_2CH int16, int32, u, 1, 2, INT32_TO_INT16_N, NOP_N
PACK_2CH int16, int32, a, 1, 2, INT32_TO_INT16_N, NOP_N

UNPACK_2CH int16, int16, u, 1, 1, NOP_N, NOP_N
UNPACK_2CH int16, int16, a, 1, 1, NOP_N, NOP_N
UNPACK_2CH int32, int32, u, 2, 2, NOP_N, NOP_N
UNPACK_2CH int32, int32, a, 2, 2, NOP_N, NOP_N
UNPACK_2CH int32, int16, u, 2, 1, INT16_TO_INT32_N, NOP_N
UNPACK_2CH int32, int16, a, 2, 1, INT16_TO_INT32_N, NOP_N
UNPACK_2CH int16, int32, u, 1, 2, INT32_TO_INT16_N, NOP_N
UNPACK_2CH int16, int32, a, 1, 2, INT32_TO_INT16_N, NOP_N

CONV float, int32, u, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
CONV float, int32, a, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
CONV int32, float, u, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
CONV int32, float, a, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
CONV float, int16, u, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
CONV float, int16, a, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
CONV int16, float, u, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT
CONV int16, float, a, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT

PACK_2CH float, int32, u, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_2CH float, int32, a, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_2CH int32, float, u, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
PACK_2CH int32, float, a, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
PACK_2CH float, int16, u, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
PACK_2CH float, int16, a, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
PACK_2CH int16, float, u, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT
PACK_2CH int16, float, a, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT

UNPACK_2CH float, int32, u, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
UNPACK_2CH float, int32, a, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
UNPACK_2CH int32, float, u, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
UNPACK_2CH int32, float, a, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
UNPACK_2CH float, int16, u, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
UNPACK_2CH float, int16, a, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
UNPACK_2CH int16, float, u, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT
UNPACK_2CH int16, float, a, 1, 2, FLOAT_TO_INT16_N, FLOAT_TO_INT16_INIT

PACK_6CH float, int32, u, 2, 2, 8, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_6CH float, int32, a, 2, 2, 8, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_6CH int32, float, u, 2, 2, 8, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
PACK_6CH int32, float, a, 2, 2, 8, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT

UNPACK_6CH float, int32, u, 2, 2, 8, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
UNPACK_6CH float, int32, a, 2, 2, 8, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
UNPACK_6CH int32, float, u, 2, 2, 8, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
UNPACK_6CH int32, float, a, 2, 2, 8, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT

PACK_8CH float, float, u, 2, 2, 9, NOP_N, NOP_N
PACK_8CH float, float, a, 2, 2, 9, NOP_N, NOP_N

PACK_8CH float, int32, u, 2, 2, 10, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_8CH float, int32, a, 2, 2, 10, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_8CH int32, float, u, 2, 2, 10, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
PACK_8CH int32, float, a, 2, 2, 10, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT

INIT_XMM ssse3
UNPACK_2CH int16, int16, u, 1, 1, NOP_N, NOP_N
UNPACK_2CH int16, int16, a, 1, 1, NOP_N, NOP_N
UNPACK_2CH int32, int16, u, 2, 1, INT16_TO_INT32_N, NOP_N
UNPACK_2CH int32, int16, a, 2, 1, INT16_TO_INT32_N, NOP_N
UNPACK_2CH float, int16, u, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT
UNPACK_2CH float, int16, a, 2, 1, INT16_TO_FLOAT_N, INT16_TO_FLOAT_INIT

%if HAVE_AVX_EXTERNAL
INIT_XMM avx
PACK_6CH float, float, u, 2, 2, 8, NOP_N, NOP_N
PACK_6CH float, float, a, 2, 2, 8, NOP_N, NOP_N

UNPACK_6CH float, float, u, 2, 2, 8, NOP_N, NOP_N
UNPACK_6CH float, float, a, 2, 2, 8, NOP_N, NOP_N

PACK_6CH float, int32, u, 2, 2, 8, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_6CH float, int32, a, 2, 2, 8, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_6CH int32, float, u, 2, 2, 8, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
PACK_6CH int32, float, a, 2, 2, 8, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT

UNPACK_6CH float, int32, u, 2, 2, 8, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
UNPACK_6CH float, int32, a, 2, 2, 8, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
UNPACK_6CH int32, float, u, 2, 2, 8, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
UNPACK_6CH int32, float, a, 2, 2, 8, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT

PACK_8CH float, float, u, 2, 2, 9, NOP_N, NOP_N
PACK_8CH float, float, a, 2, 2, 9, NOP_N, NOP_N

PACK_8CH float, int32, u, 2, 2, 10, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_8CH float, int32, a, 2, 2, 10, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
PACK_8CH int32, float, u, 2, 2, 10, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
PACK_8CH int32, float, a, 2, 2, 10, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT

INIT_YMM avx
CONV float, int32, u, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
CONV float, int32, a, 2, 2, INT32_TO_FLOAT_N, INT32_TO_FLOAT_INIT
%endif

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
CONV int32, float, u, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
CONV int32, float, a, 2, 2, FLOAT_TO_INT32_N, FLOAT_TO_INT32_INIT
%endif
