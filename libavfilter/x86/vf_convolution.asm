;*****************************************************************************
;* x86-optimized functions for convolution filter
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
half:   dd 0.5
data_p1: dd  1
data_n1: dd -1
data_p2: dd  2
data_n2: dd -2

ALIGN 64
sobel_perm: db  0, 16, 32, 48,  1, 17, 33, 49,  2, 18, 34, 50,  3, 19, 35, 51
            db  4, 20, 36, 52,  5, 21, 37, 53,  6, 22, 38, 54,  7, 23, 39, 55
            db  8, 24, 40, 56,  9, 25, 41, 57, 10, 26, 42, 58, 11, 27, 43, 59
            db 12, 28, 44, 60, 13, 29, 45, 61, 14, 30, 46, 62, 15, 31, 47, 63
sobel_mulA: db -1,  1, -2,  2
sobel_mulB: db  1, -1,  2, -2

SECTION .text

; void filter_3x3_sse4(uint8_t *dst, int width,
;                      float rdiv, float bias, const int *const matrix,
;                      const uint8_t *c[], int peak, int radius,
;                      int dstride, int stride)


%macro PROCESS_V 1
    movss m2, [matrixq + 4 * %1]
    VBROADCASTSS m2, m2
    movss m3, [c%1q + xq]
    punpcklbw m3, m6
    punpcklwd m3, m6
    pmulld m2, m3
    paddd m4, m2
%endmacro

%macro PROCESS_S 1
    movzx ptrd, byte [c%1q + xq]
    imul  ptrd, [matrixq + 4 * %1]
    add   rd, ptrd
%endmacro

%macro FILTER_3X3 0
%if UNIX64
cglobal filter_3x3, 4, 15, 7, dst, width, matrix, ptr, c0, c1, c2, c3, c4, c5, c6, c7, c8, r, x
%else
cglobal filter_3x3, 4, 15, 7, dst, width, rdiv, bias, matrix, ptr, c0, c1, c2, c3, c4, c5, c6, c7, c8, r, x
%endif

%if WIN64
    SWAP m0, m2
    SWAP m1, m3
    mov  r2q, matrixmp
    mov  r3q, ptrmp
    DEFINE_ARGS dst, width, matrix, ptr, c0, c1, c2, c3, c4, c5, c6, c7, c8, r, x
%endif
    movsxdifnidn widthq, widthd
    VBROADCASTSS m0, m0
    VBROADCASTSS m1, m1
    pxor  m6, m6
    movss m5, [half]
    VBROADCASTSS m5, m5
    mov   c0q, [ptrq + 0*gprsize]
    mov   c1q, [ptrq + 1*gprsize]
    mov   c2q, [ptrq + 2*gprsize]
    mov   c3q, [ptrq + 3*gprsize]
    mov   c4q, [ptrq + 4*gprsize]
    mov   c5q, [ptrq + 5*gprsize]
    mov   c6q, [ptrq + 6*gprsize]
    mov   c7q, [ptrq + 7*gprsize]
    mov   c8q, [ptrq + 8*gprsize]

    xor   xq, xq
    cmp   widthq, mmsize/4
    jl .loop2

    mov   rq, widthq
    and   rq, mmsize/4-1
    sub   widthq, rq

.loop1:
    pxor m4, m4         ; sum = 0;

    PROCESS_V 0
    PROCESS_V 1
    PROCESS_V 2
    PROCESS_V 3
    PROCESS_V 4
    PROCESS_V 5
    PROCESS_V 6
    PROCESS_V 7
    PROCESS_V 8

    cvtdq2ps  m4, m4
    mulps     m4, m0     ; sum *= rdiv
    addps     m4, m1     ; sum += bias
    addps     m4, m5     ; sum += 0.5
    cvttps2dq m4, m4
    packssdw  m4, m4
    packuswb  m4, m4
    movss     [dstq + xq], m4

    add xq, mmsize/4
    cmp xq, widthq
    jl .loop1

    add widthq, rq
    cmp xq, widthq
    jge .end

.loop2:
    ; reuse r to hold sum, init with zero
    xor rd, rd

    PROCESS_S 0
    PROCESS_S 1
    PROCESS_S 2
    PROCESS_S 3
    PROCESS_S 4
    PROCESS_S 5
    PROCESS_S 6
    PROCESS_S 7
    PROCESS_S 8

    pxor      m4, m4
    cvtsi2ss  m4, rd
    mulss     m4, m0     ; sum *= rdiv
    addss     m4, m1     ; sum += bias
    addss     m4, m5     ; sum += 0.5
    ; we don't have simple scalar instructions to convert
    ; from 32bit to 8bit with saturation, so here
    ; just use packed version SSE instructions for simplicity.
    cvttps2dq m4, m4     ; trunc to integer
    packssdw  m4, m4
    packuswb  m4, m4
    movd      rd, m4
    mov       [dstq + xq], rb

    add xq, 1
    cmp xq, widthq
    jl .loop2
.end:
    RET
%endmacro

%if ARCH_X86_64
INIT_XMM sse4
FILTER_3X3
%endif

%macro SOBEL_MUL 2
    movzx ptrd, byte [c%1q + xq]
    imul  ptrd, [%2]
    add   rd, ptrd
%endmacro

%macro SOBEL_ADD 1
    movzx ptrd, byte [c%1q + xq]
    add   rd, ptrd
%endmacro

; void filter_sobel_avx512(uint8_t *dst, int width,
;                      float scale, float delta, const int *const matrix,
;                      const uint8_t *c[], int peak, int radius,
;                      int dstride, int stride)
%macro FILTER_SOBEL 0
%if UNIX64
cglobal filter_sobel, 4, 15, 7, dst, width, matrix, ptr, c0, c1, c2, c3, c4, c5, c6, c7, c8, r, x
%else
cglobal filter_sobel, 4, 15, 7, dst, width, rdiv, bias, matrix, ptr, c0, c1, c2, c3, c4, c5, c6, c7, c8, r, x
%endif
%if WIN64
    VBROADCASTSS m0, xmm2
    VBROADCASTSS m1, xmm3
    mov  r2q, matrixmp
    mov  r3q, ptrmp
    DEFINE_ARGS dst, width, matrix, ptr, c0, c1, c2, c3, c4, c5, c6, c7, c8, r, x
%else
    VBROADCASTSS m0, xmm0
    VBROADCASTSS m1, xmm1
%endif
    movsxdifnidn widthq, widthd
    pxor  m6, m6
    mov   c0q, [ptrq + 0*gprsize]
    mov   c1q, [ptrq + 1*gprsize]
    mov   c2q, [ptrq + 2*gprsize]
    mov   c3q, [ptrq + 3*gprsize]
    mov   c4q, [ptrq + 4*gprsize]
    mov   c5q, [ptrq + 5*gprsize]
    mov   c6q, [ptrq + 6*gprsize]
    mov   c7q, [ptrq + 7*gprsize]
    mov   c8q, [ptrq + 8*gprsize]

    xor   xq, xq
    cmp   widthq, mmsize/4
    jl .loop2

    mov   rq, widthq
    and   rq, mmsize/4-1
    sub   widthq, rq

    mova  m6, [sobel_perm]
.loop1:
    movu          xm3, [c2q + xq]
    pmovzxbd      m5, [c0q + xq]
    vinserti32x4  ym3, [c6q + xq], 1
    pmovzxbd      m4, [c8q + xq]
    vinserti32x4  m2, m3, [c1q + xq], 2
    vinserti32x4  m3, [c5q + xq], 2
    vinserti32x4  m2, [c7q + xq], 3
    vinserti32x4  m3, [c3q + xq], 3
    vpermb        m2, m6, m2
    psubd         m4, m5
    vpermb        m3, m6, m3
    mova          m5, m4
    vpdpbusd      m4, m2, [sobel_mulA] {1to16}
    vpdpbusd      m5, m3, [sobel_mulB] {1to16}

    cvtdq2ps  m4, m4
    mulps     m4, m4

    cvtdq2ps    m5, m5
    VFMADD231PS m4, m5, m5

    sqrtps    m4, m4
    fmaddps m4, m4, m0, m1
    cvttps2dq m4, m4
    vpmovusdb [dstq + xq], m4

    add xq, mmsize/4
    cmp xq, widthq
    jl .loop1

    add widthq, rq
    cmp xq, widthq
    jge .end

.loop2:
    xor  rd, rd
    pxor m4, m4

    ;Gx
    SOBEL_MUL 0, data_n1
    SOBEL_MUL 1, data_n2
    SOBEL_MUL 2, data_n1
    SOBEL_ADD 6
    SOBEL_MUL 7, data_p2
    SOBEL_ADD 8

    cvtsi2ss xmm4, rd
    mulss    xmm4, xmm4

    xor rd, rd
    ;Gy
    SOBEL_MUL 0, data_n1
    SOBEL_ADD 2
    SOBEL_MUL 3, data_n2
    SOBEL_MUL 5, data_p2
    SOBEL_MUL 6, data_n1
    SOBEL_ADD 8

    cvtsi2ss  xmm5, rd
    fmaddss xmm4, xmm5, xmm5, xmm4

    sqrtps    xmm4, xmm4
    fmaddss   xmm4, xmm4, xm0, xm1     ;sum = sum * rdiv + bias
    cvttps2dq xmm4, xmm4     ; trunc to integer
    packssdw  xmm4, xmm4
    packuswb  xmm4, xmm4
    movd      rd, xmm4
    mov       [dstq + xq], rb

    add xq, 1
    cmp xq, widthq
    jl .loop2
.end:
    RET
%endmacro

%if ARCH_X86_64
%if HAVE_AVX512ICL_EXTERNAL
INIT_ZMM avx512icl
FILTER_SOBEL
%endif
%endif
