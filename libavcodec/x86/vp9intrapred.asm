;******************************************************************************
;* VP9 Intra prediction SIMD optimizations
;*
;* Copyright (c) 2013 Ronald S. Bultje <rsbultje gmail com>
;*
;* Parts based on:
;* H.264 intra prediction asm optimizations
;* Copyright (c) 2010 Jason Garrett-Glaser
;* Copyright (c) 2010 Holger Lubitz
;* Copyright (c) 2010 Loren Merritt
;* Copyright (c) 2010 Ronald S. Bultje
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

pw_m256: times 8 dw -256
pw_m255: times 8 dw -255
pw_512:  times 8 dw  512
pw_1024: times 8 dw 1024
pw_2048: times 8 dw 2048
pw_4096: times 8 dw 4096
pw_8192: times 8 dw 8192

pb_4x3_4x2_4x1_4x0: times 4 db 3
                    times 4 db 2
                    times 4 db 1
                    times 4 db 0
pb_8x1_8x0:   times 8 db 1
              times 8 db 0
pb_8x3_8x2:   times 8 db 3
              times 8 db 2
pb_0to5_2x7:  db 0, 1, 2, 3, 4, 5, 7, 7
              times 8 db -1
pb_0to6_9x7:  db 0, 1, 2, 3, 4, 5, 6
              times 9 db 7
pb_1to6_10x7: db 1, 2, 3, 4, 5, 6
              times 10 db 7
pb_2to6_3x7:
pb_2to6_11x7: db 2, 3, 4, 5, 6
              times 11 db 7
pb_1toE_2xF:  db 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15
pb_2toE_3xF:  db 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15
pb_13456_3xm1: db 1, 3, 4, 5, 6
               times 3 db -1
pb_6012_4xm1: db 6, 0, 1, 2
              times 4 db -1
pb_6xm1_246_8toE: times 6 db -1
                  db 2, 4, 6, 8, 9, 10, 11, 12, 13, 14
pb_6xm1_BDF_0to6: times 6 db -1
                  db 11, 13, 15, 0, 1, 2, 3, 4, 5, 6
pb_02468ACE_13579BDF: db 0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15
pb_7to1_9x0:  db 7, 6, 5, 4
pb_3to1_5x0:  db 3, 2, 1
              times 9 db 0
pb_Fto0:      db 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

pb_2:  times 16 db 2
pb_15: times 16 db 15

cextern pb_1
cextern pb_3

SECTION .text

; dc_NxN(uint8_t *dst, ptrdiff_t stride, const uint8_t *l, const uint8_t *a)

INIT_MMX ssse3
cglobal vp9_ipred_dc_4x4, 4, 4, 0, dst, stride, l, a
    movd                    m0, [lq]
    punpckldq               m0, [aq]
    pxor                    m1, m1
    psadbw                  m0, m1
    pmulhrsw                m0, [pw_4096]
    pshufb                  m0, m1
    movd      [dstq+strideq*0], m0
    movd      [dstq+strideq*1], m0
    lea                   dstq, [dstq+strideq*2]
    movd      [dstq+strideq*0], m0
    movd      [dstq+strideq*1], m0
    RET

INIT_MMX ssse3
cglobal vp9_ipred_dc_8x8, 4, 4, 0, dst, stride, l, a
    movq                    m0, [lq]
    movq                    m1, [aq]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    pxor                    m2, m2
    psadbw                  m0, m2
    psadbw                  m1, m2
    paddw                   m0, m1
    pmulhrsw                m0, [pw_2048]
    pshufb                  m0, m2
    movq      [dstq+strideq*0], m0
    movq      [dstq+strideq*1], m0
    movq      [dstq+strideq*2], m0
    movq      [dstq+stride3q ], m0
    lea                   dstq, [dstq+strideq*4]
    movq      [dstq+strideq*0], m0
    movq      [dstq+strideq*1], m0
    movq      [dstq+strideq*2], m0
    movq      [dstq+stride3q ], m0
    RET

INIT_XMM ssse3
cglobal vp9_ipred_dc_16x16, 4, 4, 3, dst, stride, l, a
    mova                    m0, [lq]
    mova                    m1, [aq]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    pxor                    m2, m2
    psadbw                  m0, m2
    psadbw                  m1, m2
    paddw                   m0, m1
    movhlps                 m1, m0
    paddw                   m0, m1
    pmulhrsw                m0, [pw_1024]
    pshufb                  m0, m2
    mov                   cntd, 4
.loop:
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    lea                   dstq, [dstq+strideq*4]
    dec                   cntd
    jg .loop
    RET

INIT_XMM ssse3
cglobal vp9_ipred_dc_32x32, 4, 4, 5, dst, stride, l, a
    mova                    m0, [lq]
    mova                    m1, [lq+16]
    mova                    m2, [aq]
    mova                    m3, [aq+16]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    pxor                    m4, m4
    psadbw                  m0, m4
    psadbw                  m1, m4
    psadbw                  m2, m4
    psadbw                  m3, m4
    paddw                   m0, m1
    paddw                   m2, m3
    paddw                   m0, m2
    movhlps                 m1, m0
    paddw                   m0, m1
    pmulhrsw                m0, [pw_512]
    pshufb                  m0, m4
    mov                   cntd, 8
.loop:
    mova   [dstq+strideq*0+ 0], m0
    mova   [dstq+strideq*0+16], m0
    mova   [dstq+strideq*1+ 0], m0
    mova   [dstq+strideq*1+16], m0
    mova   [dstq+strideq*2+ 0], m0
    mova   [dstq+strideq*2+16], m0
    mova   [dstq+stride3q + 0], m0
    mova   [dstq+stride3q +16], m0
    lea                   dstq, [dstq+strideq*4]
    dec                   cntd
    jg .loop
    RET

; dc_top/left_NxN(uint8_t *dst, ptrdiff_t stride, const uint8_t *l, const uint8_t *a)

%macro DC_1D_FUNCS 2 ; dir (top or left), arg (a or l)
INIT_MMX ssse3
cglobal vp9_ipred_dc_%1_4x4, 4, 4, 0, dst, stride, l, a
    movd                    m0, [%2q]
    pxor                    m1, m1
    psadbw                  m0, m1
    pmulhrsw                m0, [pw_8192]
    pshufb                  m0, m1
    movd      [dstq+strideq*0], m0
    movd      [dstq+strideq*1], m0
    lea                   dstq, [dstq+strideq*2]
    movd      [dstq+strideq*0], m0
    movd      [dstq+strideq*1], m0
    RET

INIT_MMX ssse3
cglobal vp9_ipred_dc_%1_8x8, 4, 4, 0, dst, stride, l, a
    movq                    m0, [%2q]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    pxor                    m1, m1
    psadbw                  m0, m1
    pmulhrsw                m0, [pw_4096]
    pshufb                  m0, m1
    movq      [dstq+strideq*0], m0
    movq      [dstq+strideq*1], m0
    movq      [dstq+strideq*2], m0
    movq      [dstq+stride3q ], m0
    lea                   dstq, [dstq+strideq*4]
    movq      [dstq+strideq*0], m0
    movq      [dstq+strideq*1], m0
    movq      [dstq+strideq*2], m0
    movq      [dstq+stride3q ], m0
    RET

INIT_XMM ssse3
cglobal vp9_ipred_dc_%1_16x16, 4, 4, 3, dst, stride, l, a
    mova                    m0, [%2q]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    pxor                    m2, m2
    psadbw                  m0, m2
    movhlps                 m1, m0
    paddw                   m0, m1
    pmulhrsw                m0, [pw_2048]
    pshufb                  m0, m2
    mov                   cntd, 4
.loop:
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    lea                   dstq, [dstq+strideq*4]
    dec                   cntd
    jg .loop
    RET

INIT_XMM ssse3
cglobal vp9_ipred_dc_%1_32x32, 4, 4, 3, dst, stride, l, a
    mova                    m0, [%2q]
    mova                    m1, [%2q+16]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    pxor                    m2, m2
    psadbw                  m0, m2
    psadbw                  m1, m2
    paddw                   m0, m1
    movhlps                 m1, m0
    paddw                   m0, m1
    pmulhrsw                m0, [pw_1024]
    pshufb                  m0, m2
    mov                   cntd, 8
.loop:
    mova   [dstq+strideq*0+ 0], m0
    mova   [dstq+strideq*0+16], m0
    mova   [dstq+strideq*1+ 0], m0
    mova   [dstq+strideq*1+16], m0
    mova   [dstq+strideq*2+ 0], m0
    mova   [dstq+strideq*2+16], m0
    mova   [dstq+stride3q + 0], m0
    mova   [dstq+stride3q +16], m0
    lea                   dstq, [dstq+strideq*4]
    dec                   cntd
    jg .loop
    RET
%endmacro

DC_1D_FUNCS top,  a
DC_1D_FUNCS left, l

; v

INIT_MMX mmx
cglobal vp9_ipred_v_8x8, 4, 4, 0, dst, stride, l, a
    movq                    m0, [aq]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    movq      [dstq+strideq*0], m0
    movq      [dstq+strideq*1], m0
    movq      [dstq+strideq*2], m0
    movq      [dstq+stride3q ], m0
    lea                   dstq, [dstq+strideq*4]
    movq      [dstq+strideq*0], m0
    movq      [dstq+strideq*1], m0
    movq      [dstq+strideq*2], m0
    movq      [dstq+stride3q ], m0
    RET

INIT_XMM sse2
cglobal vp9_ipred_v_16x16, 4, 4, 1, dst, stride, l, a
    mova                    m0, [aq]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    mov                   cntd, 4
.loop:
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    lea                   dstq, [dstq+strideq*4]
    dec                   cntd
    jg .loop
    RET

INIT_XMM sse2
cglobal vp9_ipred_v_32x32, 4, 4, 2, dst, stride, l, a
    mova                    m0, [aq]
    mova                    m1, [aq+16]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    mov                   cntd, 8
.loop:
    mova   [dstq+strideq*0+ 0], m0
    mova   [dstq+strideq*0+16], m1
    mova   [dstq+strideq*1+ 0], m0
    mova   [dstq+strideq*1+16], m1
    mova   [dstq+strideq*2+ 0], m0
    mova   [dstq+strideq*2+16], m1
    mova   [dstq+stride3q + 0], m0
    mova   [dstq+stride3q +16], m1
    lea                   dstq, [dstq+strideq*4]
    dec                   cntd
    jg .loop
    RET

; h

INIT_XMM ssse3
cglobal vp9_ipred_h_4x4, 3, 4, 1, dst, stride, l, stride3
    movd                    m0, [lq]
    pshufb                  m0, [pb_4x3_4x2_4x1_4x0]
    lea               stride3q, [strideq*3]
    movd      [dstq+strideq*0], m0
    psrldq                  m0, 4
    movd      [dstq+strideq*1], m0
    psrldq                  m0, 4
    movd      [dstq+strideq*2], m0
    psrldq                  m0, 4
    movd      [dstq+stride3q ], m0
    RET

%macro H_XMM_FUNCS 1
INIT_XMM %1
cglobal vp9_ipred_h_8x8, 3, 5, 4, dst, stride, l, stride3, cnt
    mova                    m2, [pb_8x1_8x0]
    mova                    m3, [pb_8x3_8x2]
    lea               stride3q, [strideq*3]
    mov                   cntq, 1
.loop:
    movd                    m0, [lq+cntq*4]
    pshufb                  m1, m0, m3
    pshufb                  m0, m2
    movq      [dstq+strideq*0], m1
    movhps    [dstq+strideq*1], m1
    movq      [dstq+strideq*2], m0
    movhps    [dstq+stride3q ], m0
    lea                   dstq, [dstq+strideq*4]
    dec                   cntq
    jge .loop
    RET

INIT_XMM %1
cglobal vp9_ipred_h_16x16, 3, 5, 8, dst, stride, l, stride3, cnt
    mova                    m5, [pb_1]
    mova                    m6, [pb_2]
    mova                    m7, [pb_3]
    pxor                    m4, m4
    lea               stride3q, [strideq*3]
    mov                   cntq, 3
.loop:
    movd                    m3, [lq+cntq*4]
    pshufb                  m0, m3, m7
    pshufb                  m1, m3, m6
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m1
    pshufb                  m2, m3, m5
    pshufb                  m3, m4
    mova      [dstq+strideq*2], m2
    mova      [dstq+stride3q ], m3
    lea                   dstq, [dstq+strideq*4]
    dec                   cntq
    jge .loop
    RET

INIT_XMM %1
cglobal vp9_ipred_h_32x32, 3, 5, 8, dst, stride, l, stride3, cnt
    mova                    m5, [pb_1]
    mova                    m6, [pb_2]
    mova                    m7, [pb_3]
    pxor                    m4, m4
    lea               stride3q, [strideq*3]
    mov                   cntq, 7
.loop:
    movd                    m3, [lq+cntq*4]
    pshufb                  m0, m3, m7
    pshufb                  m1, m3, m6
    mova   [dstq+strideq*0+ 0], m0
    mova   [dstq+strideq*0+16], m0
    mova   [dstq+strideq*1+ 0], m1
    mova   [dstq+strideq*1+16], m1
    pshufb                  m2, m3, m5
    pshufb                  m3, m4
    mova   [dstq+strideq*2+ 0], m2
    mova   [dstq+strideq*2+16], m2
    mova   [dstq+stride3q + 0], m3
    mova   [dstq+stride3q +16], m3
    lea                   dstq, [dstq+strideq*4]
    dec                   cntq
    jge .loop
    RET
%endmacro

H_XMM_FUNCS ssse3
H_XMM_FUNCS avx

; tm

INIT_MMX ssse3
cglobal vp9_ipred_tm_4x4, 4, 4, 0, dst, stride, l, a
    pxor                    m1, m1
    pinsrw                  m2, [aq-1], 0
    movd                    m0, [aq]
    DEFINE_ARGS dst, stride, l, cnt
    mova                    m3, [pw_m256]
    mova                    m4, [pw_m255]
    pshufb                  m2, m3
    punpcklbw               m0, m1
    psubw                   m0, m2
    mov                   cntq, 1
.loop:
    pinsrw                  m2, [lq+cntq*2], 0
    pshufb                  m1, m2, m4
    pshufb                  m2, m3
    paddw                   m1, m0
    paddw                   m2, m0
    packuswb                m1, m1
    packuswb                m2, m2
    movd      [dstq+strideq*0], m1
    movd      [dstq+strideq*1], m2
    lea                   dstq, [dstq+strideq*2]
    dec                   cntq
    jge .loop
    RET

%macro TM_XMM_FUNCS 1
INIT_XMM %1
cglobal vp9_ipred_tm_8x8, 4, 4, 5, dst, stride, l, a
    pxor                    m1, m1
    pinsrw                  m2, [aq-1], 0
    movh                    m0, [aq]
    DEFINE_ARGS dst, stride, l, cnt
    mova                    m3, [pw_m256]
    mova                    m4, [pw_m255]
    pshufb                  m2, m3
    punpcklbw               m0, m1
    psubw                   m0, m2
    mov                   cntq, 3
.loop:
    pinsrw                  m2, [lq+cntq*2], 0
    pshufb                  m1, m2, m4
    pshufb                  m2, m3
    paddw                   m1, m0
    paddw                   m2, m0
    packuswb                m1, m2
    movh      [dstq+strideq*0], m1
    movhps    [dstq+strideq*1], m1
    lea                   dstq, [dstq+strideq*2]
    dec                   cntq
    jge .loop
    RET

INIT_XMM %1
cglobal vp9_ipred_tm_16x16, 4, 4, 8, dst, stride, l, a
    pxor                    m3, m3
    pinsrw                  m2, [aq-1], 0
    mova                    m0, [aq]
    DEFINE_ARGS dst, stride, l, cnt
    mova                    m4, [pw_m256]
    mova                    m5, [pw_m255]
    pshufb                  m2, m4
    punpckhbw               m1, m0, m3
    punpcklbw               m0, m3
    psubw                   m1, m2
    psubw                   m0, m2
    mov                   cntq, 7
.loop:
    pinsrw                  m7, [lq+cntq*2], 0
    pshufb                  m3, m7, m5
    pshufb                  m7, m4
    paddw                   m2, m3, m0
    paddw                   m3, m1
    paddw                   m6, m7, m0
    paddw                   m7, m1
    packuswb                m2, m3
    packuswb                m6, m7
    mova      [dstq+strideq*0], m2
    mova      [dstq+strideq*1], m6
    lea                   dstq, [dstq+strideq*2]
    dec                   cntq
    jge .loop
    RET

%if ARCH_X86_64
INIT_XMM %1
cglobal vp9_ipred_tm_32x32, 4, 4, 14, dst, stride, l, a
    pxor                    m5, m5
    pinsrw                  m4, [aq-1], 0
    mova                    m0, [aq]
    mova                    m2, [aq+16]
    DEFINE_ARGS dst, stride, l, cnt
    mova                    m8, [pw_m256]
    mova                    m9, [pw_m255]
    pshufb                  m4, m8
    punpckhbw               m1, m0,  m5
    punpckhbw               m3, m2,  m5
    punpcklbw               m0, m5
    punpcklbw               m2, m5
    psubw                   m1, m4
    psubw                   m0, m4
    psubw                   m3, m4
    psubw                   m2, m4
    mov                   cntq, 15
.loop:
    pinsrw                 m13, [lq+cntq*2], 0
    pshufb                  m7, m13, m9
    pshufb                 m13, m8
    paddw                   m4, m7,  m0
    paddw                   m5, m7,  m1
    paddw                   m6, m7,  m2
    paddw                   m7, m3
    paddw                  m10, m13, m0
    paddw                  m11, m13, m1
    paddw                  m12, m13, m2
    paddw                  m13, m3
    packuswb                m4, m5
    packuswb                m6, m7
    packuswb               m10, m11
    packuswb               m12, m13
    mova   [dstq+strideq*0+ 0], m4
    mova   [dstq+strideq*0+16], m6
    mova   [dstq+strideq*1+ 0], m10
    mova   [dstq+strideq*1+16], m12
    lea                   dstq, [dstq+strideq*2]
    dec                   cntq
    jge .loop
    RET
%endif
%endmacro

TM_XMM_FUNCS ssse3
TM_XMM_FUNCS avx

; dl

%macro LOWPASS 4 ; left [dst], center, right, tmp
    pxor                   m%4, m%1, m%3
    pand                   m%4, [pb_1]
    pavgb                  m%1, m%3
    psubusb                m%1, m%4
    pavgb                  m%1, m%2
%endmacro

INIT_MMX ssse3
cglobal vp9_ipred_dl_4x4, 4, 4, 0, dst, stride, l, a
    movq                    m1, [aq]
    pshufb                  m0, m1, [pb_0to5_2x7]
    pshufb                  m2, m1, [pb_2to6_3x7]
    psrlq                   m1, 8
    LOWPASS                  0, 1, 2, 3

    pshufw                  m1, m0, q3321
    movd      [dstq+strideq*0], m0
    movd      [dstq+strideq*2], m1
    psrlq                   m0, 8
    psrlq                   m1, 8
    add                   dstq, strideq
    movd      [dstq+strideq*0], m0
    movd      [dstq+strideq*2], m1
    RET

%macro DL_XMM_FUNCS 1
INIT_XMM %1
cglobal vp9_ipred_dl_8x8, 4, 4, 4, dst, stride, stride5, a
    movq                    m0, [aq]
    lea               stride5q, [strideq*5]
    pshufb                  m1, m0, [pb_1to6_10x7]
    psrldq                  m2, m1, 1
    shufps                  m0, m1, q3210
    LOWPASS                  0, 1, 2, 3

    pshufd                  m1, m0, q3321
    movq      [dstq+strideq*0], m0
    movq      [dstq+strideq*4], m1
    psrldq                  m0, 1
    psrldq                  m1, 1
    movq      [dstq+strideq*1], m0
    movq      [dstq+stride5q ], m1
    lea                   dstq, [dstq+strideq*2]
    psrldq                  m0, 1
    psrldq                  m1, 1
    movq      [dstq+strideq*0], m0
    movq      [dstq+strideq*4], m1
    psrldq                  m0, 1
    psrldq                  m1, 1
    movq      [dstq+strideq*1], m0
    movq      [dstq+stride5q ], m1
    RET

INIT_XMM %1
cglobal vp9_ipred_dl_16x16, 4, 4, 6, dst, stride, l, a
    mova                    m5, [pb_1toE_2xF]
    mova                    m0, [aq]
    pshufb                  m1, m0, m5
    pshufb                  m2, m1, m5
    pshufb                  m4, m0, [pb_15]
    LOWPASS                  0, 1, 2, 3
    DEFINE_ARGS dst, stride, cnt, stride9
    lea               stride9q, [strideq*3]
    mov                   cntd, 4
    lea               stride9q, [stride9q*3]

.loop:
    movhlps                 m4, m0
    mova      [dstq+strideq*0], m0
    pshufb                  m0, m5
    mova      [dstq+strideq*8], m4
    movhlps                 m4, m0
    mova      [dstq+strideq*1], m0
    pshufb                  m0, m5
    mova      [dstq+stride9q ], m4
    lea                   dstq, [dstq+strideq*2]
    dec                   cntd
    jg .loop
    RET

INIT_XMM %1
cglobal vp9_ipred_dl_32x32, 4, 5, 8, dst, stride, cnt, a, dst16
    mova                    m5, [pb_1toE_2xF]
    mova                    m0, [aq]
    mova                    m1, [aq+16]
    palignr                 m2, m1, m0, 1
    palignr                 m3, m1, m0, 2
    LOWPASS                  0, 2, 3, 4
    pshufb                  m2, m1, m5
    pshufb                  m3, m2, m5
    pshufb                  m6, m1, [pb_15]
    LOWPASS                  1, 2, 3, 4
    mova                    m7, m6
    lea                 dst16q, [dstq  +strideq*8]
    mov                   cntd, 8
    lea                 dst16q, [dst16q+strideq*8]
.loop:
    movhlps                 m7, m1
    mova [dstq  +strideq*0+ 0], m0
    mova [dstq  +strideq*0+16], m1
    movhps [dstq+strideq*8+ 0], m0
    movq [dstq  +strideq*8+ 8], m1
    mova [dstq  +strideq*8+16], m7
    mova [dst16q+strideq*0+ 0], m1
    mova [dst16q+strideq*0+16], m6
    mova [dst16q+strideq*8+ 0], m7
    mova [dst16q+strideq*8+16], m6
%if cpuflag(avx)
    vpalignr                m0, m1, m0, 1
    pshufb                  m1, m5
%else
    palignr                 m2, m1, m0, 1
    pshufb                  m1, m5
    mova                    m0, m2
%endif
    add                   dstq, strideq
    add                 dst16q, strideq
    dec                   cntd
    jg .loop
    RET
%endmacro

DL_XMM_FUNCS ssse3
DL_XMM_FUNCS avx

; dr

INIT_MMX ssse3
cglobal vp9_ipred_dr_4x4, 4, 4, 0, dst, stride, l, a
    movd                    m0, [lq]
    punpckldq               m0, [aq-1]
    movd                    m1, [aq+3]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    palignr                 m1, m0, 1
    psrlq                   m2, m1, 8
    LOWPASS                  0, 1, 2, 3

    movd      [dstq+stride3q ], m0
    psrlq                   m0, 8
    movd      [dstq+strideq*2], m0
    psrlq                   m0, 8
    movd      [dstq+strideq*1], m0
    psrlq                   m0, 8
    movd      [dstq+strideq*0], m0
    RET

%macro DR_XMM_FUNCS 1
INIT_XMM %1
cglobal vp9_ipred_dr_8x8, 4, 4, 4, dst, stride, l, a
    movq                    m1, [lq]
    movhps                  m1, [aq-1]
    movd                    m2, [aq+7]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    pslldq                  m0, m1, 1
    palignr                 m2, m1, 1
    LOWPASS                  0, 1, 2, 3

    movhps    [dstq+strideq*0], m0
    pslldq                  m0, 1
    movhps    [dstq+strideq*1], m0
    pslldq                  m0, 1
    movhps    [dstq+strideq*2], m0
    pslldq                  m0, 1
    movhps    [dstq+stride3q ], m0
    pslldq                  m0, 1
    lea                   dstq, [dstq+strideq*4]
    movhps    [dstq+strideq*0], m0
    pslldq                  m0, 1
    movhps    [dstq+strideq*1], m0
    pslldq                  m0, 1
    movhps    [dstq+strideq*2], m0
    pslldq                  m0, 1
    movhps    [dstq+stride3q ], m0
    RET

INIT_XMM %1
cglobal vp9_ipred_dr_16x16, 4, 4, 6, dst, stride, l, a
    mova                    m1, [lq]
    movu                    m2, [aq-1]
    movd                    m4, [aq+15]
    DEFINE_ARGS dst, stride, stride9, cnt
    lea               stride9q, [strideq *3]
    mov                   cntd, 4
    lea               stride9q, [stride9q*3]
    palignr                 m4, m2, 1
    palignr                 m3, m2, m1, 15
    LOWPASS                  3,  2, 4, 5
    pslldq                  m0, m1, 1
    palignr                 m2, m1, 1
    LOWPASS                  0,  1, 2, 4

.loop:
    mova    [dstq+strideq*0  ], m3
    movhps  [dstq+strideq*8+0], m0
    movq    [dstq+strideq*8+8], m3
    palignr                 m3, m0, 15
    pslldq                  m0, 1
    mova    [dstq+strideq*1  ], m3
    movhps  [dstq+stride9q +0], m0
    movq    [dstq+stride9q +8], m3
    palignr                 m3, m0, 15
    pslldq                  m0, 1
    lea                   dstq, [dstq+strideq*2]
    dec                   cntd
    jg .loop
    RET

INIT_XMM %1
cglobal vp9_ipred_dr_32x32, 4, 4, 8, dst, stride, l, a
    mova                    m1, [lq]
    mova                    m2, [lq+16]
    movu                    m3, [aq-1]
    movu                    m4, [aq+15]
    movd                    m5, [aq+31]
    DEFINE_ARGS dst, stride, stride8, cnt
    lea               stride8q, [strideq*8]
    palignr                 m5, m4, 1
    palignr                 m6, m4, m3, 15
    LOWPASS                  5,  4,  6,  7
    palignr                 m4, m3, 1
    palignr                 m6, m3, m2, 15
    LOWPASS                  4,  3,  6,  7
    palignr                 m3, m2, 1
    palignr                 m6, m2, m1, 15
    LOWPASS                  3,  2,  6,  7
    palignr                 m2, m1, 1
    pslldq                  m0, m1, 1
    LOWPASS                  2,  1,  0,  6
    mov                   cntd, 16

    ; out=m2/m3/m4/m5
.loop:
    mova  [dstq+stride8q*0+ 0], m4
    mova  [dstq+stride8q*0+16], m5
    mova  [dstq+stride8q*2+ 0], m3
    mova  [dstq+stride8q*2+16], m4
    palignr                 m5, m4, 15
    palignr                 m4, m3, 15
    palignr                 m3, m2, 15
    pslldq                  m2, 1
    add                   dstq, strideq
    dec                   cntd
    jg .loop
    RET
%endmacro

DR_XMM_FUNCS ssse3
DR_XMM_FUNCS avx

; vl

INIT_MMX ssse3
cglobal vp9_ipred_vl_4x4, 4, 4, 0, dst, stride, l, a
    movq                    m0, [aq]
    psrlq                   m1, m0, 8
    psrlq                   m2, m1, 8
    LOWPASS                  2,  1, 0, 3
    pavgb                   m1, m0
    movd      [dstq+strideq*0], m1
    movd      [dstq+strideq*1], m2
    lea                   dstq, [dstq+strideq*2]
    psrlq                   m1, 8
    psrlq                   m2, 8
    movd      [dstq+strideq*0], m1
    movd      [dstq+strideq*1], m2
    RET

%macro VL_XMM_FUNCS 1
INIT_XMM %1
cglobal vp9_ipred_vl_8x8, 4, 4, 4, dst, stride, l, a
    movq                    m0, [aq]
    pshufb                  m0, [pb_0to6_9x7]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    psrldq                  m1, m0, 1
    psrldq                  m2, m0, 2
    LOWPASS                  2,  1,  0,  3
    pavgb                   m1, m0

    movq      [dstq+strideq*0], m1
    movq      [dstq+strideq*1], m2
    psrldq                  m1, 1
    psrldq                  m2, 1
    movq      [dstq+strideq*2], m1
    movq      [dstq+stride3q ], m2
    lea                   dstq, [dstq+strideq*4]
    psrldq                  m1, 1
    psrldq                  m2, 1
    movq      [dstq+strideq*0], m1
    movq      [dstq+strideq*1], m2
    psrldq                  m1, 1
    psrldq                  m2, 1
    movq      [dstq+strideq*2], m1
    movq      [dstq+stride3q ], m2
    RET

INIT_XMM %1
cglobal vp9_ipred_vl_16x16, 4, 4, 5, dst, stride, l, a
    mova                    m0, [aq]
    mova                    m4, [pb_1toE_2xF]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    pshufb                  m1, m0, m4
    pshufb                  m2, m1, m4
    LOWPASS                  2,  1,  0, 3
    pavgb                   m1, m0
    mov                   cntd, 4
.loop:
    mova      [dstq+strideq*0], m1
    mova      [dstq+strideq*1], m2
    pshufb                  m1, m4
    pshufb                  m2, m4
    mova      [dstq+strideq*2], m1
    mova      [dstq+stride3q ], m2
    pshufb                  m1, m4
    pshufb                  m2, m4
    lea                   dstq, [dstq+strideq*4]
    dec                   cntd
    jg .loop
    RET

INIT_XMM %1
cglobal vp9_ipred_vl_32x32, 4, 4, 7, dst, stride, l, a
    mova                    m0, [aq]
    mova                    m5, [aq+16]
    mova                    m4, [pb_1toE_2xF]
    DEFINE_ARGS dst, stride, dst16, cnt
    palignr                 m2, m5, m0, 1
    palignr                 m3, m5, m0, 2
    lea                 dst16q, [dstq  +strideq*8]
    LOWPASS                  3,  2,  0, 6
    pavgb                   m2, m0
    pshufb                  m0, m5, m4
    pshufb                  m1, m0, m4
    lea                 dst16q, [dst16q+strideq*8]
    LOWPASS                  1,  0,  5, 6
    pavgb                   m0, m5
    pshufb                  m5, [pb_15]
    mov                   cntd, 8

.loop:
%macro %%write 3
    mova    [dstq+stride%1+ 0], %2
    mova    [dstq+stride%1+16], %3
    movhps  [dst16q+stride%1 ], %2
    movu  [dst16q+stride%1+ 8], %3
    movq  [dst16q+stride%1+24], m5
%if cpuflag(avx)
    palignr                 %2, %3, %2, 1
    pshufb                  %3, m4
%else
    palignr                 m6, %3, %2, 1
    pshufb                  %3, m4
    mova                    %2, m6
%endif
%endmacro

    %%write                q*0, m2, m0
    %%write                q*1, m3, m1
    lea                   dstq, [dstq  +strideq*2]
    lea                 dst16q, [dst16q+strideq*2]
    dec                   cntd
    jg .loop
    RET
%endmacro

VL_XMM_FUNCS ssse3
VL_XMM_FUNCS avx

; vr

INIT_MMX ssse3
cglobal vp9_ipred_vr_4x4, 4, 4, 0, dst, stride, l, a
    movq                    m1, [aq-1]
    punpckldq               m2, [lq]
    movd                    m0, [aq]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    pavgb                   m0, m1
    palignr                 m1, m2, 5
    psrlq                   m2, m1, 8
    psllq                   m3, m1, 8
    LOWPASS                  2,  1, 3, 4

    ; ABCD <- for the following predictor:
    ; EFGH
    ; IABC  | m0 contains ABCDxxxx
    ; JEFG  | m2 contains xJIEFGHx

    punpckldq               m0, m2
    pshufb                  m2, [pb_13456_3xm1]
    movd      [dstq+strideq*0], m0
    pshufb                  m0, [pb_6012_4xm1]
    movd      [dstq+stride3q ], m2
    psrlq                   m2, 8
    movd      [dstq+strideq*2], m0
    movd      [dstq+strideq*1], m2
    RET

%macro VR_XMM_FUNCS 1
INIT_XMM %1
cglobal vp9_ipred_vr_8x8, 4, 4, 5, dst, stride, l, a
    movu                    m1, [aq-1]
    movhps                  m2, [lq]
    movq                    m0, [aq]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    pavgb                   m0, m1
    palignr                 m1, m2, 9
    pslldq                  m2, m1, 1
    pslldq                  m3, m1, 2
    LOWPASS                  1,  2, 3, 4

    ; ABCDEFGH <- for the following predictor:
    ; IJKLMNOP
    ; QABCDEFG  | m0 contains ABCDEFGHxxxxxxxx
    ; RIJKLMNO  | m1 contains xxVUTSRQIJKLMNOP
    ; SQABCDEF
    ; TRIJKLMN
    ; USQABCDE
    ; VTRIJKLM

    punpcklqdq              m0, m1 ; ABCDEFGHxxVUTSRQ
    movq      [dstq+strideq*0], m0
    pshufb                  m0, [pb_6xm1_BDF_0to6] ; xxxxxxUSQABCDEFG
    movhps    [dstq+strideq*1], m1
    pshufb                  m1, [pb_6xm1_246_8toE] ; xxxxxxVTRIJKLMNO
    movhps    [dstq+strideq*2], m0
    pslldq                  m0, 1
    movhps    [dstq+stride3q ], m1
    lea                   dstq, [dstq+strideq*4]
    pslldq                  m1, 1
    movhps    [dstq+strideq*0], m0
    pslldq                  m0, 1
    movhps    [dstq+strideq*1], m1
    pslldq                  m1, 1
    movhps    [dstq+strideq*2], m0
    movhps    [dstq+stride3q ], m1
    RET

INIT_XMM %1
cglobal vp9_ipred_vr_16x16, 4, 4, 6, dst, stride, l, a
    mova                    m0, [aq]
    movu                    m1, [aq-1]
    mova                    m2, [lq]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    palignr                 m3, m1, m2, 15
    LOWPASS                  3,  1,  0,  4
    pavgb                   m0, m1
    palignr                 m1, m2,  1
    pslldq                  m4, m2,  1
    LOWPASS                  1,  2,  4,  5
    pshufb                  m1, [pb_02468ACE_13579BDF]
    mov                   cntd, 4

.loop:
    movlhps                 m2, m1
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m3
    palignr                 m4, m0, m1, 15
    palignr                 m5, m3, m2, 15
    mova      [dstq+strideq*2], m4
    mova      [dstq+stride3q ], m5
    lea                   dstq, [dstq+strideq*4]
    palignr                 m0, m1, 14
    palignr                 m3, m2, 14
    pslldq                  m1, 2
    dec                   cntd
    jg .loop
    RET

%if ARCH_X86_64
INIT_XMM %1
cglobal vp9_ipred_vr_32x32, 4, 4, 9, dst, stride, l, a
    mova                    m0, [aq]
    mova                    m2, [aq+16]
    movu                    m1, [aq-1]
    palignr                 m3, m2, m0, 15
    palignr                 m4, m2, m0, 14
    LOWPASS                  4,  3,  2,  5
    pavgb                   m3, m2
    mova                    m2, [lq+16]
    palignr                 m5, m1, m2, 15
    LOWPASS                  5,  1,  0,  6
    pavgb                   m0, m1
    mova                    m6, [lq]
    palignr                 m1, m2,  1
    palignr                 m7, m2, m6, 15
    LOWPASS                  1,  2,  7,  8
    palignr                 m2, m6,  1
    pslldq                  m7, m6,  1
    LOWPASS                  2,  6,  7,  8
    pshufb                  m1, [pb_02468ACE_13579BDF]
    pshufb                  m2, [pb_02468ACE_13579BDF]
    DEFINE_ARGS dst, stride, dst16, cnt
    lea                 dst16q, [dstq  +strideq*8]
    lea                 dst16q, [dst16q+strideq*8]
    SBUTTERFLY             qdq,  2,  1,  6
    mov                   cntd, 8

.loop:
    ; even lines (0, 2, 4, ...): m1 | m0, m3
    ;  odd lines (1, 3, 5, ...): m2 | m5, m4
%macro %%write 4
    mova    [dstq+stride%1+ 0], %3
    mova    [dstq+stride%1+16], %4
    movhps  [dst16q+stride%1 ], %2
    movu  [dst16q+stride%1+ 8], %3
    movq  [dst16q+stride%1+24], %4
    palignr                 %4, %3, 15
    palignr                 %3, %2, 15
    pslldq                  %2,  1
%endmacro

    %%write                q*0, m1, m0, m3
    %%write                q*1, m2, m5, m4
    lea                   dstq, [dstq  +strideq*2]
    lea                 dst16q, [dst16q+strideq*2]
    dec                   cntd
    jg .loop
    RET
%endif
%endmacro

VR_XMM_FUNCS ssse3
VR_XMM_FUNCS avx

; hd

INIT_MMX ssse3
cglobal vp9_ipred_hd_4x4, 4, 4, 0, dst, stride, l, a
    movd                    m0, [lq]
    punpckldq               m0, [aq-1]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    psrlq                   m1, m0, 8
    psrlq                   m2, m1, 8
    LOWPASS                  2,  1, 0,  3
    pavgb                   m1, m0

    ; DHIJ <- for the following predictor:
    ; CGDH
    ; BFCG  | m1 contains ABCDxxxx
    ; AEBF  | m2 contains EFGHIJxx

    punpcklbw               m1, m2
    punpckhdq               m0, m1, m2

    ; m1 contains AEBFCGDH
    ; m0 contains CGDHIJxx

    movd      [dstq+stride3q ], m1
    movd      [dstq+strideq*1], m0
    psrlq                   m1, 16
    psrlq                   m0, 16
    movd      [dstq+strideq*2], m1
    movd      [dstq+strideq*0], m0
    RET

%macro HD_XMM_FUNCS 1
INIT_XMM %1
cglobal vp9_ipred_hd_8x8, 4, 4, 4, dst, stride, l, a
    movq                    m0, [lq]
    movhps                  m0, [aq-1]
    DEFINE_ARGS dst, stride, stride3, dst4
    lea               stride3q, [strideq*3]
    lea                  dst4q, [dstq+strideq*4]
    psrldq                  m1, m0, 1
    psrldq                  m2, m1, 1
    LOWPASS                  2,  1,  0,  3
    pavgb                   m1, m0

    ; HPQRSTUV <- for the following predictor
    ; GOHPQRST
    ; FNGOHPQR  | m1 contains ABCDEFGHxxxxxxxx
    ; EMFNGOHP  | m2 contains IJKLMNOPQRSTUVxx
    ; DLEMFNGO
    ; CKDLEMFN
    ; BJCKDLEM
    ; AIBJCKDL

    punpcklbw               m1, m2
    movhlps                 m2, m2

    ; m1 contains AIBJCKDLEMFNGOHP
    ; m2 contains QRSTUVxxxxxxxxxx

    movhps   [dstq +stride3q ], m1
    movq     [dst4q+stride3q ], m1
    palignr                 m3, m2, m1, 2
    movhps   [dstq +strideq*2], m3
    movq     [dst4q+strideq*2], m3
    palignr                 m3, m2, m1, 4
    movhps   [dstq +strideq*1], m3
    movq     [dst4q+strideq*1], m3
    palignr                 m2, m1, 6
    movhps   [dstq +strideq*0], m2
    movq     [dst4q+strideq*0], m2
    RET

INIT_XMM %1
cglobal vp9_ipred_hd_16x16, 4, 6, 7, dst, stride, l, a
    mova                    m0, [lq]
    movu                    m3, [aq-1]
    DEFINE_ARGS dst, stride, stride4, dst4, dst8, dst12
    lea               stride4q, [strideq*4]
    lea                  dst4q, [dstq +stride4q]
    lea                  dst8q, [dst4q+stride4q]
    lea                 dst12q, [dst8q+stride4q]
    psrldq                  m4, m3,  1
    psrldq                  m5, m3,  2
    LOWPASS                  5,  4,  3,  6
    palignr                 m1, m3, m0,  1
    palignr                 m2, m3, m0,  2
    LOWPASS                  2,  1,  0,  6
    pavgb                   m1, m0
    SBUTTERFLY              bw,  1,  2,  6

    ; I PROBABLY INVERTED L0 ad L16 here
    ; m1, m2, m5
.loop:
    sub               stride4q, strideq
    movhps [dstq +stride4q +0], m2
    movq   [dstq +stride4q +8], m5
    mova   [dst4q+stride4q   ], m2
    movhps [dst8q+stride4q +0], m1
    movq   [dst8q+stride4q +8], m2
    mova  [dst12q+stride4q   ], m1
%if cpuflag(avx)
    palignr                 m1, m2, m1, 2
    palignr                 m2, m5, m2, 2
%else
    palignr                 m3, m2, m1, 2
    palignr                 m0, m5, m2, 2
    mova                    m1, m3
    mova                    m2, m0
%endif
    psrldq                  m5, 2
    jg .loop
    RET

INIT_XMM %1
cglobal vp9_ipred_hd_32x32, 4, 6, 8, dst, stride, l, a
    mova                    m0, [lq]
    mova                    m1, [lq+16]
    movu                    m2, [aq-1]
    movu                    m3, [aq+15]
    DEFINE_ARGS dst, stride, stride8, dst8, dst16, dst24
    lea               stride8q, [strideq*8]
    lea                  dst8q, [dstq  +stride8q]
    lea                 dst16q, [dst8q +stride8q]
    lea                 dst24q, [dst16q+stride8q]
    psrldq                  m4, m3,  1
    psrldq                  m5, m3,  2
    LOWPASS                  5,  4,  3,  6
    palignr                 m4, m3, m2,  2
    palignr                 m3, m2,  1
    LOWPASS                  4,  3,  2,  6
    palignr                 m3, m2, m1,  2
    palignr                 m2, m1,  1
    LOWPASS                  3,  2,  1,  6
    pavgb                   m2, m1
    palignr                 m6, m1, m0,  1
    palignr                 m1, m0,  2
    LOWPASS                  1,  6,  0,  7
    pavgb                   m0, m1
    SBUTTERFLY              bw,  2,  3,  6
    SBUTTERFLY              bw,  0,  1,  6

    ; m0, m1, m2, m3, m4, m5
.loop:
    sub               stride8q, strideq
    mova  [dstq  +stride8q+ 0], m3
    mova  [dstq  +stride8q+16], m4
    mova  [dst8q +stride8q+ 0], m2
    mova  [dst8q +stride8q+16], m3
    mova  [dst16q+stride8q+ 0], m1
    mova  [dst16q+stride8q+16], m2
    mova  [dst24q+stride8q+ 0], m0
    mova  [dst24q+stride8q+16], m1
%if cpuflag(avx)
    palignr                 m0, m1, m0, 2
    palignr                 m1, m2, m1, 2
    palignr                 m2, m3, m2, 2
    palignr                 m3, m4, m3, 2
    palignr                 m4, m5, m4, 2
    psrldq                  m5, 2
%else
    psrldq                  m6, m5, 2
    palignr                 m5, m4, 2
    palignr                 m4, m3, 2
    palignr                 m3, m2, 2
    palignr                 m2, m1, 2
    palignr                 m1, m0, 2
    mova                    m0, m1
    mova                    m1, m2
    mova                    m2, m3
    mova                    m3, m4
    mova                    m4, m5
    mova                    m5, m6
%endif
    jg .loop
    RET
%endmacro

HD_XMM_FUNCS ssse3
HD_XMM_FUNCS avx

INIT_MMX ssse3
cglobal vp9_ipred_hu_4x4, 3, 3, 0, dst, stride, l
    movd                    m0, [lq]
    pshufb                  m0, [pb_3to1_5x0]
    psrlq                   m1, m0, 8
    psrlq                   m2, m1, 8
    LOWPASS                  2,  1, 0, 3
    pavgb                   m1, m0
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    SBUTTERFLY              bw,  1, 2, 0
    palignr                 m2, m1, 2
    movd      [dstq+strideq*0], m1
    movd      [dstq+strideq*1], m2
    punpckhdq               m1, m1
    punpckhdq               m2, m2
    movd      [dstq+strideq*2], m1
    movd      [dstq+stride3q ], m2
    RET

%macro HU_XMM_FUNCS 1
INIT_XMM %1
cglobal vp9_ipred_hu_8x8, 3, 4, 4, dst, stride, l
    movq                    m0, [lq]
    pshufb                  m0, [pb_7to1_9x0]
    psrldq                  m1, m0, 1
    psrldq                  m2, m1, 1
    LOWPASS                  2,  1, 0, 3
    pavgb                   m1, m0
    DEFINE_ARGS dst, stride, stride3, dst4
    lea               stride3q, [strideq*3]
    lea                  dst4q, [dstq+strideq*4]
    SBUTTERFLY              bw,  1, 2, 0
    movq     [dstq +strideq*0], m1
    movhps   [dst4q+strideq*0], m1
    palignr                 m0, m2, m1, 2
    movq     [dstq +strideq*1], m0
    movhps   [dst4q+strideq*1], m0
    palignr                 m0, m2, m1, 4
    movq     [dstq +strideq*2], m0
    movhps   [dst4q+strideq*2], m0
    palignr                 m2, m1, 6
    movq     [dstq +stride3q ], m2
    movhps   [dst4q+stride3q ], m2
    RET

INIT_XMM %1
cglobal vp9_ipred_hu_16x16, 3, 4, 5, dst, stride, l
    mova                    m0, [lq]
    pshufb                  m0, [pb_Fto0]
    mova                    m3, [pb_2toE_3xF]
    pshufb                  m1, m0, [pb_1toE_2xF]
    pshufb                  m2, m0, m3
    LOWPASS                  2,  1,  0,  4
    pavgb                   m1, m0
    DEFINE_ARGS dst, stride, stride9, cnt
    lea                stride9q, [strideq *3]
    mov                   cntd,  4
    lea                stride9q, [stride9q*3]
    SBUTTERFLY              bw,  1,  2,  0

.loop:
    mova      [dstq+strideq*0], m1
    mova      [dstq+strideq*8], m2
    palignr                 m0, m2, m1, 2
    pshufb                  m2, m3
    mova      [dstq+strideq*1], m0
    mova      [dstq+stride9q ], m2
    palignr                 m1, m2, m0, 2
    pshufb                  m2, m3
    lea                   dstq, [dstq+strideq*2]
    dec                   cntd
    jg .loop
    RET

INIT_XMM %1
cglobal vp9_ipred_hu_32x32, 3, 7, 7, dst, stride, l
    mova                    m0, [lq]
    mova                    m1, [lq+16]
    mova                    m2, [pb_Fto0]
    mova                    m4, [pb_2toE_3xF]
    pshufb                  m0, m2
    pshufb                  m1, m2
    palignr                 m2, m0, m1,  1
    palignr                 m3, m0, m1,  2
    LOWPASS                  3,  2,  1,  5
    pavgb                   m2, m1
    pshufb                  m1, m0, m4
    pshufb                  m5, m0, [pb_1toE_2xF]
    LOWPASS                  1,  5,  0,  6
    pavgb                   m0, m5
    DEFINE_ARGS dst, stride, cnt, stride0, dst8, dst16, dst24
    mov                   cntd,  8
    xor               stride0q, stride0q
    lea                  dst8q, [dstq  +strideq*8]
    lea                 dst16q, [dst8q +strideq*8]
    lea                 dst24q, [dst16q+strideq*8]
    SBUTTERFLY              bw,  0,  1,  5
    SBUTTERFLY              bw,  2,  3,  5
    pshufb                  m6, m1, [pb_15]

.loop:
    mova  [dstq  +stride0q+ 0], m2
    mova  [dstq  +stride0q+16], m3
    mova  [dst8q +stride0q+ 0], m3
    mova  [dst8q +stride0q+16], m0
    mova  [dst16q+stride0q+ 0], m0
    mova  [dst16q+stride0q+16], m1
    mova  [dst24q+stride0q+ 0], m1
    mova  [dst24q+stride0q+16], m6
%if cpuflag(avx)
    palignr                 m2, m3, m2, 2
    palignr                 m3, m0, m3, 2
    palignr                 m0, m1, m0, 2
    pshufb                  m1, m4
%else
    pshufb                  m5, m1, m4
    palignr                 m1, m0, 2
    palignr                 m0, m3, 2
    palignr                 m3, m2, 2
    mova                    m2, m3
    mova                    m3, m0
    mova                    m0, m1
    mova                    m1, m5
%endif
    add               stride0q, strideq
    dec                   cntd
    jg .loop
    RET
%endmacro

HU_XMM_FUNCS ssse3
HU_XMM_FUNCS avx

; FIXME 127, 128, 129 ?
