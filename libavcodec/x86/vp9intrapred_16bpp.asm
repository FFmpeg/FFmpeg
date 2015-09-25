;******************************************************************************
;* VP9 Intra prediction SIMD optimizations
;*
;* Copyright (c) 2015 Ronald S. Bultje <rsbultje gmail com>
;* Copyright (c) 2015 Henrik Gramner <henrik gramner com>
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

pd_2: times 8 dd 2
pd_4: times 8 dd 4
pd_8: times 8 dd 8

cextern pw_1
cextern pw_1023
cextern pw_4095
cextern pd_16
cextern pd_32

SECTION .text

INIT_MMX mmx
cglobal vp9_ipred_v_4x4_16, 2, 4, 1, dst, stride, l, a
    movifnidn               aq, amp
    mova                    m0, [aq]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    RET

INIT_XMM sse
cglobal vp9_ipred_v_8x8_16, 2, 4, 1, dst, stride, l, a
    movifnidn               aq, amp
    mova                    m0, [aq]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    lea                   dstq, [dstq+strideq*4]
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    RET

INIT_XMM sse
cglobal vp9_ipred_v_16x16_16, 2, 4, 2, dst, stride, l, a
    movifnidn               aq, amp
    mova                    m0, [aq]
    mova                    m1, [aq+mmsize]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    mov                   cntd, 4
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
    dec               cntd
    jg .loop
    RET

INIT_XMM sse
cglobal vp9_ipred_v_32x32_16, 2, 4, 4, dst, stride, l, a
    movifnidn               aq, amp
    mova                    m0, [aq+mmsize*0]
    mova                    m1, [aq+mmsize*1]
    mova                    m2, [aq+mmsize*2]
    mova                    m3, [aq+mmsize*3]
    DEFINE_ARGS dst, stride, cnt
    mov                   cntd, 16
.loop:
    mova   [dstq+strideq*0+ 0], m0
    mova   [dstq+strideq*0+16], m1
    mova   [dstq+strideq*0+32], m2
    mova   [dstq+strideq*0+48], m3
    mova   [dstq+strideq*1+ 0], m0
    mova   [dstq+strideq*1+16], m1
    mova   [dstq+strideq*1+32], m2
    mova   [dstq+strideq*1+48], m3
    lea                   dstq, [dstq+strideq*2]
    dec               cntd
    jg .loop
    RET

INIT_MMX mmxext
cglobal vp9_ipred_h_4x4_16, 3, 3, 4, dst, stride, l, a
    mova                    m3, [lq]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    pshufw                  m0, m3, q3333
    pshufw                  m1, m3, q2222
    pshufw                  m2, m3, q1111
    pshufw                  m3, m3, q0000
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m1
    mova      [dstq+strideq*2], m2
    mova      [dstq+stride3q ], m3
    RET

INIT_XMM sse2
cglobal vp9_ipred_h_8x8_16, 3, 3, 4, dst, stride, l, a
    mova                    m2, [lq]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    punpckhwd               m3, m2, m2
    pshufd                  m0, m3, q3333
    pshufd                  m1, m3, q2222
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m1
    pshufd                  m0, m3, q1111
    pshufd                  m1, m3, q0000
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m1
    lea                   dstq, [dstq+strideq*4]
    punpcklwd               m2, m2
    pshufd                  m0, m2, q3333
    pshufd                  m1, m2, q2222
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m1
    pshufd                  m0, m2, q1111
    pshufd                  m1, m2, q0000
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m1
    RET

INIT_XMM sse2
cglobal vp9_ipred_h_16x16_16, 3, 5, 4, dst, stride, l, stride3, cnt
    mov                   cntd, 3
    lea               stride3q, [strideq*3]
.loop:
    movh                    m3, [lq+cntq*8]
    punpcklwd               m3, m3
    pshufd                  m0, m3, q3333
    pshufd                  m1, m3, q2222
    pshufd                  m2, m3, q1111
    pshufd                  m3, m3, q0000
    mova    [dstq+strideq*0+ 0], m0
    mova    [dstq+strideq*0+16], m0
    mova    [dstq+strideq*1+ 0], m1
    mova    [dstq+strideq*1+16], m1
    mova    [dstq+strideq*2+ 0], m2
    mova    [dstq+strideq*2+16], m2
    mova    [dstq+stride3q + 0], m3
    mova    [dstq+stride3q +16], m3
    lea                   dstq, [dstq+strideq*4]
    dec                   cntd
    jge .loop
    RET

INIT_XMM sse2
cglobal vp9_ipred_h_32x32_16, 3, 5, 4, dst, stride, l, stride3, cnt
    mov                   cntd, 7
    lea               stride3q, [strideq*3]
.loop:
    movh                    m3, [lq+cntq*8]
    punpcklwd               m3, m3
    pshufd                  m0, m3, q3333
    pshufd                  m1, m3, q2222
    pshufd                  m2, m3, q1111
    pshufd                  m3, m3, q0000
    mova   [dstq+strideq*0+ 0], m0
    mova   [dstq+strideq*0+16], m0
    mova   [dstq+strideq*0+32], m0
    mova   [dstq+strideq*0+48], m0
    mova   [dstq+strideq*1+ 0], m1
    mova   [dstq+strideq*1+16], m1
    mova   [dstq+strideq*1+32], m1
    mova   [dstq+strideq*1+48], m1
    mova   [dstq+strideq*2+ 0], m2
    mova   [dstq+strideq*2+16], m2
    mova   [dstq+strideq*2+32], m2
    mova   [dstq+strideq*2+48], m2
    mova   [dstq+stride3q + 0], m3
    mova   [dstq+stride3q +16], m3
    mova   [dstq+stride3q +32], m3
    mova   [dstq+stride3q +48], m3
    lea                   dstq, [dstq+strideq*4]
    dec                   cntd
    jge .loop
    RET

INIT_MMX mmxext
cglobal vp9_ipred_dc_4x4_16, 4, 4, 2, dst, stride, l, a
    mova                    m0, [lq]
    paddw                   m0, [aq]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    pmaddwd                 m0, [pw_1]
    pshufw                  m1, m0, q3232
    paddd                   m0, [pd_4]
    paddd                   m0, m1
    psrad                   m0, 3
    pshufw                  m0, m0, q0000
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    RET

INIT_XMM sse2
cglobal vp9_ipred_dc_8x8_16, 4, 4, 2, dst, stride, l, a
    mova                    m0, [lq]
    paddw                   m0, [aq]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    pmaddwd                 m0, [pw_1]
    pshufd                  m1, m0, q3232
    paddd                   m0, m1
    pshufd                  m1, m0, q1111
    paddd                   m0, [pd_8]
    paddd                   m0, m1
    psrad                   m0, 4
    pshuflw                 m0, m0, q0000
    punpcklqdq              m0, m0
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    lea                   dstq, [dstq+strideq*4]
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    RET

INIT_XMM sse2
cglobal vp9_ipred_dc_16x16_16, 4, 4, 2, dst, stride, l, a
    mova                    m0, [lq]
    paddw                   m0, [lq+mmsize]
    paddw                   m0, [aq]
    paddw                   m0, [aq+mmsize]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    mov                   cntd, 4
    pmaddwd                 m0, [pw_1]
    pshufd                  m1, m0, q3232
    paddd                   m0, m1
    pshufd                  m1, m0, q1111
    paddd                   m0, [pd_16]
    paddd                   m0, m1
    psrad                   m0, 5
    pshuflw                 m0, m0, q0000
    punpcklqdq              m0, m0
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

INIT_XMM sse2
cglobal vp9_ipred_dc_32x32_16, 4, 4, 2, dst, stride, l, a
    mova                    m0, [lq+mmsize*0]
    paddw                   m0, [lq+mmsize*1]
    paddw                   m0, [lq+mmsize*2]
    paddw                   m0, [lq+mmsize*3]
    paddw                   m0, [aq+mmsize*0]
    paddw                   m0, [aq+mmsize*1]
    paddw                   m0, [aq+mmsize*2]
    paddw                   m0, [aq+mmsize*3]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    mov                   cntd, 16
    pmaddwd                 m0, [pw_1]
    pshufd                  m1, m0, q3232
    paddd                   m0, m1
    pshufd                  m1, m0, q1111
    paddd                   m0, [pd_32]
    paddd                   m0, m1
    psrad                   m0, 6
    pshuflw                 m0, m0, q0000
    punpcklqdq              m0, m0
.loop:
    mova   [dstq+strideq*0+ 0], m0
    mova   [dstq+strideq*0+16], m0
    mova   [dstq+strideq*0+32], m0
    mova   [dstq+strideq*0+48], m0
    mova   [dstq+strideq*1+ 0], m0
    mova   [dstq+strideq*1+16], m0
    mova   [dstq+strideq*1+32], m0
    mova   [dstq+strideq*1+48], m0
    lea                   dstq, [dstq+strideq*2]
    dec                   cntd
    jg .loop
    RET

%macro DC_1D_FNS 2
INIT_MMX mmxext
cglobal vp9_ipred_dc_%1_4x4_16, 4, 4, 2, dst, stride, l, a
    mova                    m0, [%2]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    pmaddwd                 m0, [pw_1]
    pshufw                  m1, m0, q3232
    paddd                   m0, [pd_2]
    paddd                   m0, m1
    psrad                   m0, 2
    pshufw                  m0, m0, q0000
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    RET

INIT_XMM sse2
cglobal vp9_ipred_dc_%1_8x8_16, 4, 4, 2, dst, stride, l, a
    mova                    m0, [%2]
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    pmaddwd                 m0, [pw_1]
    pshufd                  m1, m0, q3232
    paddd                   m0, m1
    pshufd                  m1, m0, q1111
    paddd                   m0, [pd_4]
    paddd                   m0, m1
    psrad                   m0, 3
    pshuflw                 m0, m0, q0000
    punpcklqdq              m0, m0
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    lea                   dstq, [dstq+strideq*4]
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m0
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m0
    RET

INIT_XMM sse2
cglobal vp9_ipred_dc_%1_16x16_16, 4, 4, 2, dst, stride, l, a
    mova                    m0, [%2]
    paddw                   m0, [%2+mmsize]
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    mov                   cntd, 4
    pmaddwd                 m0, [pw_1]
    pshufd                  m1, m0, q3232
    paddd                   m0, m1
    pshufd                  m1, m0, q1111
    paddd                   m0, [pd_8]
    paddd                   m0, m1
    psrad                   m0, 4
    pshuflw                 m0, m0, q0000
    punpcklqdq              m0, m0
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

INIT_XMM sse2
cglobal vp9_ipred_dc_%1_32x32_16, 4, 4, 2, dst, stride, l, a
    mova                    m0, [%2+mmsize*0]
    paddw                   m0, [%2+mmsize*1]
    paddw                   m0, [%2+mmsize*2]
    paddw                   m0, [%2+mmsize*3]
    DEFINE_ARGS dst, stride, cnt
    mov                   cntd, 16
    pmaddwd                 m0, [pw_1]
    pshufd                  m1, m0, q3232
    paddd                   m0, m1
    pshufd                  m1, m0, q1111
    paddd                   m0, [pd_16]
    paddd                   m0, m1
    psrad                   m0, 5
    pshuflw                 m0, m0, q0000
    punpcklqdq              m0, m0
.loop:
    mova   [dstq+strideq*0+ 0], m0
    mova   [dstq+strideq*0+16], m0
    mova   [dstq+strideq*0+32], m0
    mova   [dstq+strideq*0+48], m0
    mova   [dstq+strideq*1+ 0], m0
    mova   [dstq+strideq*1+16], m0
    mova   [dstq+strideq*1+32], m0
    mova   [dstq+strideq*1+48], m0
    lea                   dstq, [dstq+strideq*2]
    dec                   cntd
    jg .loop
    RET
%endmacro

DC_1D_FNS top,  aq
DC_1D_FNS left, lq

INIT_MMX mmxext
cglobal vp9_ipred_tm_4x4_10, 4, 4, 6, dst, stride, l, a
    mova                    m5, [pw_1023]
.body:
    mova                    m4, [aq]
    mova                    m3, [lq]
    movd                    m0, [aq-4]
    pshufw                  m0, m0, q1111
    psubw                   m4, m0
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
    pshufw                  m0, m3, q3333
    pshufw                  m1, m3, q2222
    pshufw                  m2, m3, q1111
    pshufw                  m3, m3, q0000
    paddw                   m0, m4
    paddw                   m1, m4
    paddw                   m2, m4
    paddw                   m3, m4
    pxor                    m4, m4
    pmaxsw                  m0, m4
    pmaxsw                  m1, m4
    pmaxsw                  m2, m4
    pmaxsw                  m3, m4
    pminsw                  m0, m5
    pminsw                  m1, m5
    pminsw                  m2, m5
    pminsw                  m3, m5
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m1
    mova      [dstq+strideq*2], m2
    mova      [dstq+stride3q ], m3
    RET

cglobal vp9_ipred_tm_4x4_12, 4, 4, 6, dst, stride, l, a
    mova                    m5, [pw_4095]
    jmp mangle(private_prefix %+ _ %+ vp9_ipred_tm_4x4_10 %+ SUFFIX).body

INIT_XMM sse2
cglobal vp9_ipred_tm_8x8_10, 4, 5, 7, dst, stride, l, a
    mova                    m4, [pw_1023]
.body:
    pxor                    m6, m6
    mova                    m5, [aq]
    movd                    m0, [aq-4]
    pshuflw                 m0, m0, q1111
    punpcklqdq              m0, m0
    psubw                   m5, m0
    DEFINE_ARGS dst, stride, l, stride3, cnt
    lea               stride3q, [strideq*3]
    mov                   cntd, 1
.loop:
    movh                    m3, [lq+cntq*8]
    punpcklwd               m3, m3
    pshufd                  m0, m3, q3333
    pshufd                  m1, m3, q2222
    pshufd                  m2, m3, q1111
    pshufd                  m3, m3, q0000
    paddw                   m0, m5
    paddw                   m1, m5
    paddw                   m2, m5
    paddw                   m3, m5
    pmaxsw                  m0, m6
    pmaxsw                  m1, m6
    pmaxsw                  m2, m6
    pmaxsw                  m3, m6
    pminsw                  m0, m4
    pminsw                  m1, m4
    pminsw                  m2, m4
    pminsw                  m3, m4
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m1
    mova      [dstq+strideq*2], m2
    mova      [dstq+stride3q ], m3
    lea                   dstq, [dstq+strideq*4]
    dec                   cntd
    jge .loop
    RET

cglobal vp9_ipred_tm_8x8_12, 4, 5, 7, dst, stride, l, a
    mova                    m4, [pw_4095]
    jmp mangle(private_prefix %+ _ %+ vp9_ipred_tm_8x8_10 %+ SUFFIX).body

INIT_XMM sse2
cglobal vp9_ipred_tm_16x16_10, 4, 4, 8, dst, stride, l, a
    mova                    m7, [pw_1023]
.body:
    pxor                    m6, m6
    mova                    m4, [aq]
    mova                    m5, [aq+mmsize]
    movd                    m0, [aq-4]
    pshuflw                 m0, m0, q1111
    punpcklqdq              m0, m0
    psubw                   m4, m0
    psubw                   m5, m0
    DEFINE_ARGS dst, stride, l, cnt
    mov                   cntd, 7
.loop:
    movd                    m3, [lq+cntq*4]
    punpcklwd               m3, m3
    pshufd                  m2, m3, q1111
    pshufd                  m3, m3, q0000
    paddw                   m0, m2, m4
    paddw                   m2, m5
    paddw                   m1, m3, m4
    paddw                   m3, m5
    pmaxsw                  m0, m6
    pmaxsw                  m2, m6
    pmaxsw                  m1, m6
    pmaxsw                  m3, m6
    pminsw                  m0, m7
    pminsw                  m2, m7
    pminsw                  m1, m7
    pminsw                  m3, m7
    mova   [dstq+strideq*0+ 0], m0
    mova   [dstq+strideq*0+16], m2
    mova   [dstq+strideq*1+ 0], m1
    mova   [dstq+strideq*1+16], m3
    lea                   dstq, [dstq+strideq*2]
    dec                   cntd
    jge .loop
    RET

cglobal vp9_ipred_tm_16x16_12, 4, 4, 8, dst, stride, l, a
    mova                    m7, [pw_4095]
    jmp mangle(private_prefix %+ _ %+ vp9_ipred_tm_16x16_10 %+ SUFFIX).body

INIT_XMM sse2
cglobal vp9_ipred_tm_32x32_10, 4, 4, 10, 32 * ARCH_X86_32, dst, stride, l, a
    mova                    m0, [pw_1023]
.body:
    pxor                    m1, m1
%if ARCH_X86_64
    SWAP                     0, 8
    SWAP                     1, 9
%define reg_min m9
%define reg_max m8
%else
    mova              [rsp+ 0], m0
    mova              [rsp+16], m1
%define reg_min [rsp+16]
%define reg_max [rsp+ 0]
%endif

    mova                    m4, [aq+mmsize*0]
    mova                    m5, [aq+mmsize*1]
    mova                    m6, [aq+mmsize*2]
    mova                    m7, [aq+mmsize*3]
    movd                    m0, [aq-4]
    pshuflw                 m0, m0, q1111
    punpcklqdq              m0, m0
    psubw                   m4, m0
    psubw                   m5, m0
    psubw                   m6, m0
    psubw                   m7, m0
    DEFINE_ARGS dst, stride, l, cnt
    mov                   cntd, 31
.loop:
    pinsrw                  m3, [lq+cntq*2], 0
    punpcklwd               m3, m3
    pshufd                  m3, m3, q0000
    paddw                   m0, m3, m4
    paddw                   m1, m3, m5
    paddw                   m2, m3, m6
    paddw                   m3, m7
    pmaxsw                  m0, reg_min
    pmaxsw                  m1, reg_min
    pmaxsw                  m2, reg_min
    pmaxsw                  m3, reg_min
    pminsw                  m0, reg_max
    pminsw                  m1, reg_max
    pminsw                  m2, reg_max
    pminsw                  m3, reg_max
    mova   [dstq+strideq*0+ 0], m0
    mova   [dstq+strideq*0+16], m1
    mova   [dstq+strideq*0+32], m2
    mova   [dstq+strideq*0+48], m3
    add                   dstq, strideq
    dec                   cntd
    jge .loop
    RET

cglobal vp9_ipred_tm_32x32_12, 4, 4, 10, 32 * ARCH_X86_32, dst, stride, l, a
    mova                    m0, [pw_4095]
    jmp mangle(private_prefix %+ _ %+ vp9_ipred_tm_32x32_10 %+ SUFFIX).body
