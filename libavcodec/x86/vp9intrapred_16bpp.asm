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

pb_2to15_14_15: db 2,3,4,5,6,7,8,9,10,11,12,13,14,15,14,15
pb_4_5_8to13_8x0: db 4,5,8,9,10,11,12,13,0,0,0,0,0,0,0,0
pb_0to7_67x4: db 0,1,2,3,4,5,6,7,6,7,6,7,6,7,6,7

cextern pw_1
cextern pw_1023
cextern pw_4095
cextern pd_16
cextern pd_32
cextern pd_65535;

; FIXME most top-only functions (ddl, vl, v, dc_top) can be modified to take
; only 3 registers on x86-32, which would make it one cycle faster, but that
; would make the code quite a bit uglier...

SECTION .text

%macro SCRATCH 3-4
%if ARCH_X86_64
    SWAP                %1, %2
%if %0 == 4
%define reg_%4 m%2
%endif
%else
    mova              [%3], m%1
%if %0 == 4
%define reg_%4 [%3]
%endif
%endif
%endmacro

%macro UNSCRATCH 3-4
%if ARCH_X86_64
    SWAP                %1, %2
%else
    mova               m%1, [%3]
%endif
%if %0 == 4
%undef reg_%4
%endif
%endmacro

%macro PRELOAD 2-3
%if ARCH_X86_64
    mova               m%1, [%2]
%if %0 == 3
%define reg_%3 m%1
%endif
%elif %0 == 3
%define reg_%3 [%2]
%endif
%endmacro

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
cglobal vp9_ipred_tm_32x32_10, 4, 4, 10, 32 * -ARCH_X86_32, dst, stride, l, a
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

cglobal vp9_ipred_tm_32x32_12, 4, 4, 10, 32 * -ARCH_X86_32, dst, stride, l, a
    mova                    m0, [pw_4095]
    jmp mangle(private_prefix %+ _ %+ vp9_ipred_tm_32x32_10 %+ SUFFIX).body

; Directional intra predicion functions
;
; in the functions below, 'abcdefgh' refers to above data (sometimes simply
; abbreviated as a[N-M]). 'stuvwxyz' refers to left data (sometimes simply
; abbreviated as l[N-M]). * is top-left data. ABCDEFG or A[N-M] is filtered
; above data, STUVWXYZ or L[N-M] is filtered left data, and # is filtered
; top-left data.

; left=(left+2*center+right+2)>>2
%macro LOWPASS 3 ; left [dst], center, right
    paddw                  m%1, m%3
    psraw                  m%1, 1
    pavgw                  m%1, m%2
%endmacro

; abcdefgh (src) -> bcdefghh (dst)
; dst/src can be the same register
%macro SHIFT_RIGHT 2-3 [pb_2to15_14_15] ; dst, src, [ssse3_shift_reg]
%if cpuflag(ssse3)
    pshufb                  %1, %2, %3              ; abcdefgh -> bcdefghh
%else
    psrldq                  %1, %2, 2               ; abcdefgh -> bcdefgh.
    pshufhw                 %1, %1, q2210           ; bcdefgh. -> bcdefghh
%endif
%endmacro

; abcdefgh (src) -> bcdefghh (dst1) and cdefghhh (dst2)
%macro SHIFT_RIGHTx2 3-4 [pb_2to15_14_15] ; dst1, dst2, src, [ssse3_shift_reg]
%if cpuflag(ssse3)
    pshufb                  %1, %3, %4              ; abcdefgh -> bcdefghh
    pshufb                  %2, %1, %4              ; bcdefghh -> cdefghhh
%else
    psrldq                  %1, %3, 2               ; abcdefgh -> bcdefgh.
    psrldq                  %2, %3, 4               ; abcdefgh -> cdefgh..
    pshufhw                 %1, %1, q2210           ; bcdefgh. -> bcdefghh
    pshufhw                 %2, %2, q1110           ; cdefgh.. -> cdefghhh
%endif
%endmacro

%macro DL_FUNCS 0
cglobal vp9_ipred_dl_4x4_16, 2, 4, 3, dst, stride, l, a
    movifnidn               aq, amp
    movu                    m1, [aq]                ; abcdefgh
    pshufhw                 m0, m1, q3310           ; abcdefhh
    SHIFT_RIGHT             m1, m1                  ; bcdefghh
    psrldq                  m2, m1, 2               ; cdefghh.
    LOWPASS                  0,  1,  2              ; BCDEFGh.
    pshufd                  m1, m0, q3321           ; DEFGh...
    movh      [dstq+strideq*0], m0
    movh      [dstq+strideq*2], m1
    add                   dstq, strideq
    psrldq                  m0, 2                   ; CDEFGh..
    psrldq                  m1, 2                   ; EFGh....
    movh      [dstq+strideq*0], m0
    movh      [dstq+strideq*2], m1
    RET

cglobal vp9_ipred_dl_8x8_16, 2, 4, 5, dst, stride, l, a
    movifnidn               aq, amp
    mova                    m0, [aq]                ; abcdefgh
%if cpuflag(ssse3)
    mova                    m4, [pb_2to15_14_15]
%endif
    SHIFT_RIGHTx2           m1, m2, m0, m4          ; bcdefghh/cdefghhh
    LOWPASS                  0,  1,  2              ; BCDEFGHh
    shufps                  m1, m0, m2, q3332       ; FGHhhhhh
    shufps                  m3, m0, m1, q2121       ; DEFGHhhh
    DEFINE_ARGS dst, stride, stride5
    lea               stride5q, [strideq*5]

    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*4], m1
    SHIFT_RIGHT             m0, m0, m4              ; CDEFGHhh
    pshuflw                 m1, m1, q3321           ; GHhhhhhh
    pshufd                  m2, m0, q3321           ; EFGHhhhh
    mova      [dstq+strideq*1], m0
    mova      [dstq+stride5q ], m1
    lea                   dstq, [dstq+strideq*2]
    pshuflw                 m1, m1, q3321           ; Hhhhhhhh
    mova      [dstq+strideq*0], m3
    mova      [dstq+strideq*4], m1
    pshuflw                 m1, m1, q3321           ; hhhhhhhh
    mova      [dstq+strideq*1], m2
    mova      [dstq+stride5q ], m1
    RET

cglobal vp9_ipred_dl_16x16_16, 2, 4, 5, dst, stride, l, a
    movifnidn               aq, amp
    mova                    m0, [aq]                ; abcdefgh
    mova                    m3, [aq+mmsize]         ; ijklmnop
    PALIGNR                 m1, m3, m0, 2, m4       ; bcdefghi
    PALIGNR                 m2, m3, m0, 4, m4       ; cdefghij
    LOWPASS                  0,  1,  2              ; BCDEFGHI
%if cpuflag(ssse3)
    mova                    m4, [pb_2to15_14_15]
%endif
    SHIFT_RIGHTx2           m2, m1, m3, m4          ; jklmnopp/klmnoppp
    LOWPASS                  1,  2,  3              ; JKLMNOPp
    pshufd                  m2, m2, q3333           ; pppppppp
    DEFINE_ARGS dst, stride, cnt
    mov                   cntd, 8

.loop:
    mova   [dstq+strideq*0+ 0], m0
    mova   [dstq+strideq*0+16], m1
    mova   [dstq+strideq*8+ 0], m1
    mova   [dstq+strideq*8+16], m2
    add                   dstq, strideq
%if cpuflag(avx)
    vpalignr                m0, m1, m0, 2
%else
    PALIGNR                 m3, m1, m0, 2, m4
    mova                    m0, m3
%endif
    SHIFT_RIGHT             m1, m1, m4
    dec                   cntd
    jg .loop
    RET

cglobal vp9_ipred_dl_32x32_16, 2, 5, 7, dst, stride, l, a
    movifnidn               aq, amp
    mova                    m0, [aq+mmsize*0]       ; abcdefgh
    mova                    m1, [aq+mmsize*1]       ; ijklmnop
    mova                    m2, [aq+mmsize*2]       ; qrstuvwx
    mova                    m3, [aq+mmsize*3]       ; yz012345
    PALIGNR                 m4, m1, m0, 2, m6
    PALIGNR                 m5, m1, m0, 4, m6
    LOWPASS                  0,  4,  5              ; BCDEFGHI
    PALIGNR                 m4, m2, m1, 2, m6
    PALIGNR                 m5, m2, m1, 4, m6
    LOWPASS                  1,  4,  5              ; JKLMNOPQ
    PALIGNR                 m4, m3, m2, 2, m6
    PALIGNR                 m5, m3, m2, 4, m6
    LOWPASS                  2,  4,  5              ; RSTUVWXY
%if cpuflag(ssse3)
    mova                    m6, [pb_2to15_14_15]
%endif
    SHIFT_RIGHTx2           m4, m5, m3, m6
    LOWPASS                  3,  4,  5              ; Z0123455
    pshufd                  m4, m4, q3333           ; 55555555
    DEFINE_ARGS dst, stride, stride8, stride24, cnt
    mov                   cntd, 8
    lea               stride8q, [strideq*8]
    lea              stride24q, [stride8q*3]

.loop:
    mova  [dstq+stride8q*0+ 0], m0
    mova  [dstq+stride8q*0+16], m1
    mova  [dstq+stride8q*0+32], m2
    mova  [dstq+stride8q*0+48], m3
    mova  [dstq+stride8q*1+ 0], m1
    mova  [dstq+stride8q*1+16], m2
    mova  [dstq+stride8q*1+32], m3
    mova  [dstq+stride8q*1+48], m4
    mova  [dstq+stride8q*2+ 0], m2
    mova  [dstq+stride8q*2+16], m3
    mova  [dstq+stride8q*2+32], m4
    mova  [dstq+stride8q*2+48], m4
    mova  [dstq+stride24q + 0], m3
    mova  [dstq+stride24q +16], m4
    mova  [dstq+stride24q +32], m4
    mova  [dstq+stride24q +48], m4
    add                   dstq, strideq
%if cpuflag(avx)
    vpalignr                m0, m1, m0, 2
    vpalignr                m1, m2, m1, 2
    vpalignr                m2, m3, m2, 2
%else
    PALIGNR                 m5, m1, m0, 2, m6
    mova                    m0, m5
    PALIGNR                 m5, m2, m1, 2, m6
    mova                    m1, m5
    PALIGNR                 m5, m3, m2, 2, m6
    mova                    m2, m5
%endif
    SHIFT_RIGHT             m3, m3, m6
    dec                   cntd
    jg .loop
    RET
%endmacro

INIT_XMM sse2
DL_FUNCS
INIT_XMM ssse3
DL_FUNCS
INIT_XMM avx
DL_FUNCS

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
cglobal vp9_ipred_dl_16x16_16, 2, 4, 5, dst, stride, l, a
    movifnidn               aq, amp
    mova                    m0, [aq]                   ; abcdefghijklmnop
    vpbroadcastw           xm1, [aq+30]                ; pppppppp
    vperm2i128              m2, m0, m1, q0201          ; ijklmnoppppppppp
    vpalignr                m3, m2, m0, 2              ; bcdefghijklmnopp
    vpalignr                m4, m2, m0, 4              ; cdefghijklmnoppp
    LOWPASS                  0,  3,  4                 ; BCDEFGHIJKLMNOPp
    vperm2i128              m2, m0, m1, q0201          ; JKLMNOPppppppppp
    DEFINE_ARGS dst, stride, stride3, cnt
    mov                   cntd, 2
    lea               stride3q, [strideq*3]
.loop:
    mova      [dstq+strideq*0], m0
    vpalignr                m3, m2, m0, 2
    vpalignr                m4, m2, m0, 4
    mova      [dstq+strideq*1], m3
    mova      [dstq+strideq*2], m4
    vpalignr                m3, m2, m0, 6
    vpalignr                m4, m2, m0, 8
    mova      [dstq+stride3q ], m3
    lea                   dstq, [dstq+strideq*4]
    mova      [dstq+strideq*0], m4
    vpalignr                m3, m2, m0, 10
    vpalignr                m4, m2, m0, 12
    mova      [dstq+strideq*1], m3
    mova      [dstq+strideq*2], m4
    vpalignr                m3, m2, m0, 14
    mova      [dstq+stride3q ], m3
    lea                   dstq, [dstq+strideq*4]
    mova                    m0, m2
    vperm2i128              m2, m2, m2, q0101          ; pppppppppppppppp
    dec                   cntd
    jg .loop
    RET
%endif

%macro DR_FUNCS 1 ; stack_mem_for_32x32_32bit_function
cglobal vp9_ipred_dr_4x4_16, 4, 4, 3, dst, stride, l, a
    movh                    m0, [lq]                ; wxyz....
    movhps                  m0, [aq-2]              ; wxyz*abc
    movd                    m1, [aq+6]              ; d.......
    PALIGNR                 m1, m0, 2, m2           ; xyz*abcd
    psrldq                  m2, m1, 2               ; yz*abcd.
    LOWPASS                  0, 1, 2                ; XYZ#ABC.
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]

    movh      [dstq+stride3q ], m0
    psrldq                  m0, 2                   ; YZ#ABC..
    movh      [dstq+strideq*2], m0
    psrldq                  m0, 2                   ; Z#ABC...
    movh      [dstq+strideq*1], m0
    psrldq                  m0, 2                   ; #ABC....
    movh      [dstq+strideq*0], m0
    RET

cglobal vp9_ipred_dr_8x8_16, 4, 4, 5, dst, stride, l, a
    mova                    m0, [lq]                ; stuvwxyz
    movu                    m1, [aq-2]              ; *abcdefg
    mova                    m2, [aq]                ; abcdefgh
    psrldq                  m3, m2, 2               ; bcdefgh.
    LOWPASS                  3,  2, 1               ; ABCDEFG.
    PALIGNR                 m1, m0, 2, m4           ; tuvwxyz*
    PALIGNR                 m2, m1, 2, m4           ; uvwxyz*a
    LOWPASS                  2,  1, 0               ; TUVWXYZ#
    DEFINE_ARGS dst, stride, dst4, stride3
    lea               stride3q, [strideq*3]
    lea                  dst4q, [dstq+strideq*4]

    movhps [dstq +stride3q +0], m2
    movh   [dstq+ stride3q +8], m3
    mova   [dst4q+stride3q +0], m2
    PALIGNR                 m1, m3, m2, 2, m0
    psrldq                  m3, 2
    movhps [dstq +strideq*2+0], m1
    movh   [dstq+ strideq*2+8], m3
    mova   [dst4q+strideq*2+0], m1
    PALIGNR                 m2, m3, m1, 2, m0
    psrldq                  m3, 2
    movhps [dstq +strideq*1+0], m2
    movh   [dstq+ strideq*1+8], m3
    mova   [dst4q+strideq*1+0], m2
    PALIGNR                 m1, m3, m2, 2, m0
    psrldq                  m3, 2
    movhps [dstq +strideq*0+0], m1
    movh   [dstq+ strideq*0+8], m3
    mova   [dst4q+strideq*0+0], m1
    RET

cglobal vp9_ipred_dr_16x16_16, 4, 4, 7, dst, stride, l, a
    mova                    m0, [lq]                ; klmnopqr
    mova                    m1, [lq+mmsize]         ; stuvwxyz
    movu                    m2, [aq-2]              ; *abcdefg
    movu                    m3, [aq+mmsize-2]       ; hijklmno
    mova                    m4, [aq]                ; abcdefgh
    mova                    m5, [aq+mmsize]         ; ijklmnop
    psrldq                  m6, m5, 2               ; jklmnop.
    LOWPASS                  6,  5, 3               ; IJKLMNO.
    PALIGNR                 m5, m4, 2, m3           ; bcdefghi
    LOWPASS                  5,  4, 2               ; ABCDEFGH
    PALIGNR                 m2, m1, 2, m3           ; tuvwxyz*
    PALIGNR                 m4, m2, 2, m3           ; uvwxyz*a
    LOWPASS                  4,  2, 1               ; TUVWXYZ#
    PALIGNR                 m1, m0, 2, m3           ; lmnopqrs
    PALIGNR                 m2, m1, 2, m3           ; mnopqrst
    LOWPASS                  2, 1, 0                ; LMNOPQRS
    DEFINE_ARGS dst, stride, dst8, cnt
    lea                  dst8q, [dstq+strideq*8]
    mov                   cntd, 8

.loop:
    sub                  dst8q, strideq
    mova  [dst8q+strideq*0+ 0], m4
    mova  [dst8q+strideq*0+16], m5
    mova  [dst8q+strideq*8+ 0], m2
    mova  [dst8q+strideq*8+16], m4
%if cpuflag(avx)
    vpalignr                m2, m4, m2, 2
    vpalignr                m4, m5, m4, 2
    vpalignr                m5, m6, m5, 2
%else
    PALIGNR                 m0, m4, m2, 2, m1
    mova                    m2, m0
    PALIGNR                 m0, m5, m4, 2, m1
    mova                    m4, m0
    PALIGNR                 m0, m6, m5, 2, m1
    mova                    m5, m0
%endif
    psrldq                  m6, 2
    dec                   cntd
    jg .loop
    RET

cglobal vp9_ipred_dr_32x32_16, 4, 5, 10 + notcpuflag(ssse3), \
                               %1 * ARCH_X86_32 * -mmsize, dst, stride, l, a
    mova                    m0, [aq+mmsize*3]       ; a[24-31]
    movu                    m1, [aq+mmsize*3-2]     ; a[23-30]
    psrldq                  m2, m0, 2               ; a[25-31].
    LOWPASS                  2,  0, 1               ; A[24-30].
    mova                    m1, [aq+mmsize*2]       ; a[16-23]
    movu                    m3, [aq+mmsize*2-2]     ; a[15-22]
    PALIGNR                 m0, m1, 2, m4           ; a[17-24]
    LOWPASS                  0,  1, 3               ; A[16-23]
    mova                    m3, [aq+mmsize*1]       ; a[8-15]
    movu                    m4, [aq+mmsize*1-2]     ; a[7-14]
    PALIGNR                 m1, m3, 2, m5           ; a[9-16]
    LOWPASS                  1,  3, 4               ; A[8-15]
    mova                    m4, [aq+mmsize*0]       ; a[0-7]
    movu                    m5, [aq+mmsize*0-2]     ; *a[0-6]
    PALIGNR                 m3, m4, 2, m6           ; a[1-8]
    LOWPASS                  3,  4, 5               ; A[0-7]
    SCRATCH                  1,  8, rsp+0*mmsize
    SCRATCH                  3,  9, rsp+1*mmsize
%if notcpuflag(ssse3)
    SCRATCH                  0, 10, rsp+2*mmsize
%endif
    mova                    m6, [lq+mmsize*3]       ; l[24-31]
    PALIGNR                 m5, m6, 2, m0           ; l[25-31]*
    PALIGNR                 m4, m5, 2, m0           ; l[26-31]*a
    LOWPASS                  4,  5, 6               ; L[25-31]#
    mova                    m7, [lq+mmsize*2]       ; l[16-23]
    PALIGNR                 m6, m7, 2, m0           ; l[17-24]
    PALIGNR                 m5, m6, 2, m0           ; l[18-25]
    LOWPASS                  5,  6, 7               ; L[17-24]
    mova                    m1, [lq+mmsize*1]       ; l[8-15]
    PALIGNR                 m7, m1, 2, m0           ; l[9-16]
    PALIGNR                 m6, m7, 2, m0           ; l[10-17]
    LOWPASS                  6,  7, 1               ; L[9-16]
    mova                    m3, [lq+mmsize*0]       ; l[0-7]
    PALIGNR                 m1, m3, 2, m0           ; l[1-8]
    PALIGNR                 m7, m1, 2, m0           ; l[2-9]
    LOWPASS                  7,  1, 3               ; L[1-8]
%if cpuflag(ssse3)
%if cpuflag(avx)
    UNSCRATCH                1,  8, rsp+0*mmsize
%endif
    UNSCRATCH                3,  9, rsp+1*mmsize
%else
    UNSCRATCH                0, 10, rsp+2*mmsize
%endif
    DEFINE_ARGS dst8, stride, stride8, stride24, cnt
    lea               stride8q, [strideq*8]
    lea              stride24q, [stride8q*3]
    lea                  dst8q, [dst8q+strideq*8]
    mov                   cntd, 8

.loop:
    sub                  dst8q, strideq
%if notcpuflag(avx)
    UNSCRATCH                1,  8, rsp+0*mmsize
%if notcpuflag(ssse3)
    UNSCRATCH                3,  9, rsp+1*mmsize
%endif
%endif
    mova [dst8q+stride8q*0+ 0], m4
    mova [dst8q+stride8q*0+16], m3
    mova [dst8q+stride8q*0+32], m1
    mova [dst8q+stride8q*0+48], m0
    mova [dst8q+stride8q*1+ 0], m5
    mova [dst8q+stride8q*1+16], m4
    mova [dst8q+stride8q*1+32], m3
    mova [dst8q+stride8q*1+48], m1
    mova [dst8q+stride8q*2+ 0], m6
    mova [dst8q+stride8q*2+16], m5
    mova [dst8q+stride8q*2+32], m4
    mova [dst8q+stride8q*2+48], m3
    mova [dst8q+stride24q + 0], m7
    mova [dst8q+stride24q +16], m6
    mova [dst8q+stride24q +32], m5
    mova [dst8q+stride24q +48], m4
%if cpuflag(avx)
    vpalignr                m7, m6, m7, 2
    vpalignr                m6, m5, m6, 2
    vpalignr                m5, m4, m5, 2
    vpalignr                m4, m3, m4, 2
    vpalignr                m3, m1, m3, 2
    vpalignr                m1, m0, m1, 2
    vpalignr                m0, m2, m0, 2
%else
    SCRATCH                  2,  8, rsp+0*mmsize
%if notcpuflag(ssse3)
    SCRATCH                  0,  9, rsp+1*mmsize
%endif
    PALIGNR                 m2, m6, m7, 2, m0
    mova                    m7, m2
    PALIGNR                 m2, m5, m6, 2, m0
    mova                    m6, m2
    PALIGNR                 m2, m4, m5, 2, m0
    mova                    m5, m2
    PALIGNR                 m2, m3, m4, 2, m0
    mova                    m4, m2
    PALIGNR                 m2, m1, m3, 2, m0
    mova                    m3, m2
%if notcpuflag(ssse3)
    UNSCRATCH                0,  9, rsp+1*mmsize
    SCRATCH                  3,  9, rsp+1*mmsize
%endif
    PALIGNR                 m2, m0, m1, 2, m3
    mova                    m1, m2
    UNSCRATCH                2,  8, rsp+0*mmsize
    SCRATCH                  1,  8, rsp+0*mmsize
    PALIGNR                 m1, m2, m0, 2, m3
    mova                    m0, m1
%endif
    psrldq                  m2, 2
    dec                   cntd
    jg .loop
    RET
%endmacro

INIT_XMM sse2
DR_FUNCS 3
INIT_XMM ssse3
DR_FUNCS 2
INIT_XMM avx
DR_FUNCS 2

%macro VL_FUNCS 1 ; stack_mem_for_32x32_32bit_function
cglobal vp9_ipred_vl_4x4_16, 2, 4, 3, dst, stride, l, a
    movifnidn               aq, amp
    movu                    m0, [aq]                ; abcdefgh
    psrldq                  m1, m0, 2               ; bcdefgh.
    psrldq                  m2, m0, 4               ; cdefgh..
    LOWPASS                  2,  1, 0               ; BCDEFGH.
    pavgw                   m1, m0                  ; ABCDEFG.
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]

    movh      [dstq+strideq*0], m1
    movh      [dstq+strideq*1], m2
    psrldq                  m1, 2
    psrldq                  m2, 2
    movh      [dstq+strideq*2], m1
    movh      [dstq+stride3q ], m2
    RET

cglobal vp9_ipred_vl_8x8_16, 2, 4, 4, dst, stride, l, a
    movifnidn               aq, amp
    mova                    m0, [aq]                ; abcdefgh
%if cpuflag(ssse3)
    mova                    m3, [pb_2to15_14_15]
%endif
    SHIFT_RIGHTx2           m1, m2, m0, m3          ; bcdefghh/cdefghhh
    LOWPASS                  2,  1, 0               ; BCDEFGHh
    pavgw                   m1, m0                  ; ABCDEFGh
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]

    mova      [dstq+strideq*0], m1
    mova      [dstq+strideq*1], m2
    SHIFT_RIGHT             m1, m1, m3
    SHIFT_RIGHT             m2, m2, m3
    mova      [dstq+strideq*2], m1
    mova      [dstq+stride3q ], m2
    lea                   dstq, [dstq+strideq*4]
    SHIFT_RIGHT             m1, m1, m3
    SHIFT_RIGHT             m2, m2, m3
    mova      [dstq+strideq*0], m1
    mova      [dstq+strideq*1], m2
    SHIFT_RIGHT             m1, m1, m3
    SHIFT_RIGHT             m2, m2, m3
    mova      [dstq+strideq*2], m1
    mova      [dstq+stride3q ], m2
    RET

cglobal vp9_ipred_vl_16x16_16, 2, 4, 6, dst, stride, l, a
    movifnidn               aq, amp
    mova                    m0, [aq]
    mova                    m1, [aq+mmsize]
    PALIGNR                 m2, m1, m0, 2, m3
    PALIGNR                 m3, m1, m0, 4, m4
    LOWPASS                  3,  2,  0
    pavgw                   m2, m0
%if cpuflag(ssse3)
    mova                    m4, [pb_2to15_14_15]
%endif
    SHIFT_RIGHTx2           m5, m0, m1, m4
    LOWPASS                  0,  5,  1
    pavgw                   m1, m5
    DEFINE_ARGS dst, stride, cnt
    mov                   cntd, 8

.loop:
    mova   [dstq+strideq*0+ 0], m2
    mova   [dstq+strideq*0+16], m1
    mova   [dstq+strideq*1+ 0], m3
    mova   [dstq+strideq*1+16], m0
    lea                   dstq, [dstq+strideq*2]
%if cpuflag(avx)
    vpalignr                m2, m1, m2, 2
    vpalignr                m3, m0, m3, 2
%else
    PALIGNR                 m5, m1, m2, 2, m4
    mova                    m2, m5
    PALIGNR                 m5, m0, m3, 2, m4
    mova                    m3, m5
%endif
    SHIFT_RIGHT             m1, m1, m4
    SHIFT_RIGHT             m0, m0, m4
    dec                   cntd
    jg .loop
    RET

cglobal vp9_ipred_vl_32x32_16, 2, 5, 11, %1 * mmsize * ARCH_X86_32, dst, stride, l, a
    movifnidn               aq, amp
    mova                    m0, [aq+mmsize*0]
    mova                    m1, [aq+mmsize*1]
    mova                    m2, [aq+mmsize*2]
    PALIGNR                 m6, m1, m0, 2, m5
    PALIGNR                 m7, m1, m0, 4, m5
    LOWPASS                  7,  6,  0
    pavgw                   m6, m0
    SCRATCH                  6,  8, rsp+0*mmsize
    PALIGNR                 m4, m2, m1, 2, m0
    PALIGNR                 m5, m2, m1, 4, m0
    LOWPASS                  5,  4,  1
    pavgw                   m4, m1
    mova                    m0, [aq+mmsize*3]
    PALIGNR                 m1, m0, m2, 2, m6
    PALIGNR                 m3, m0, m2, 4, m6
    LOWPASS                  3,  1,  2
    pavgw                   m2, m1
%if cpuflag(ssse3)
    PRELOAD                 10, pb_2to15_14_15, shuf
%endif
    SHIFT_RIGHTx2           m6, m1, m0, reg_shuf
    LOWPASS                  1,  6,  0
    pavgw                   m0, m6
%if ARCH_X86_64
    pshufd                  m9, m6, q3333
%endif
%if cpuflag(avx)
    UNSCRATCH                6,  8, rsp+0*mmsize
%endif
    DEFINE_ARGS dst, stride, cnt, stride16, stride17
    mov              stride16q, strideq
    mov                   cntd, 8
    shl              stride16q, 4
    lea              stride17q, [stride16q+strideq]

    ; FIXME m8 is unused for avx, so we could save one register here for win64
.loop:
%if notcpuflag(avx)
    UNSCRATCH                6,  8, rsp+0*mmsize
%endif
    mova   [dstq+strideq*0+ 0], m6
    mova   [dstq+strideq*0+16], m4
    mova   [dstq+strideq*0+32], m2
    mova   [dstq+strideq*0+48], m0
    mova   [dstq+strideq*1+ 0], m7
    mova   [dstq+strideq*1+16], m5
    mova   [dstq+strideq*1+32], m3
    mova   [dstq+strideq*1+48], m1
    mova   [dstq+stride16q+ 0], m4
    mova   [dstq+stride16q+16], m2
    mova   [dstq+stride16q+32], m0
%if ARCH_X86_64
    mova   [dstq+stride16q+48], m9
%endif
    mova   [dstq+stride17q+ 0], m5
    mova   [dstq+stride17q+16], m3
    mova   [dstq+stride17q+32], m1
%if ARCH_X86_64
    mova   [dstq+stride17q+48], m9
%endif
    lea                   dstq, [dstq+strideq*2]
%if cpuflag(avx)
    vpalignr                m6, m4, m6, 2
    vpalignr                m4, m2, m4, 2
    vpalignr                m2, m0, m2, 2
    vpalignr                m7, m5, m7, 2
    vpalignr                m5, m3, m5, 2
    vpalignr                m3, m1, m3, 2
%else
    SCRATCH                  3,  8, rsp+0*mmsize
%if notcpuflag(ssse3)
    SCRATCH                  1, 10, rsp+1*mmsize
%endif
    PALIGNR                 m3, m4, m6, 2, m1
    mova                    m6, m3
    PALIGNR                 m3, m2, m4, 2, m1
    mova                    m4, m3
    PALIGNR                 m3, m0, m2, 2, m1
    mova                    m2, m3
    PALIGNR                 m3, m5, m7, 2, m1
    mova                    m7, m3
    UNSCRATCH                3,  8, rsp+0*mmsize
    SCRATCH                  6,  8, rsp+0*mmsize
%if notcpuflag(ssse3)
    UNSCRATCH                1, 10, rsp+1*mmsize
    SCRATCH                  7, 10, rsp+1*mmsize
%endif
    PALIGNR                 m6, m3, m5, 2, m7
    mova                    m5, m6
    PALIGNR                 m6, m1, m3, 2, m7
    mova                    m3, m6
%if notcpuflag(ssse3)
    UNSCRATCH                7, 10, rsp+1*mmsize
%endif
%endif
    SHIFT_RIGHT             m1, m1, reg_shuf
    SHIFT_RIGHT             m0, m0, reg_shuf
    dec                   cntd
    jg .loop

%if ARCH_X86_32
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]
%assign %%n 0
%rep 4
    mova   [dstq+strideq*0+48], m0
    mova   [dstq+strideq*1+48], m0
    mova   [dstq+strideq*2+48], m0
    mova   [dstq+stride3q +48], m0
%if %%n < 3
    lea                   dstq, [dstq+strideq*4]
%endif
%assign %%n (%%n+1)
%endrep
%endif
    RET
%endmacro

INIT_XMM sse2
VL_FUNCS 2
INIT_XMM ssse3
VL_FUNCS 1
INIT_XMM avx
VL_FUNCS 1

%macro VR_FUNCS 0
cglobal vp9_ipred_vr_4x4_16, 4, 4, 3, dst, stride, l, a
    movu                    m0, [aq-2]
    movhps                  m1, [lq]
    PALIGNR                 m0, m1, 10, m2          ; xyz*abcd
    pslldq                  m1, m0, 2               ; .xyz*abc
    pslldq                  m2, m0, 4               ; ..xyz*ab
    LOWPASS                  2,  1, 0               ; ..YZ#ABC
    pavgw                   m1, m0                  ; ....#ABC
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]

    movhps    [dstq+strideq*0], m1
    movhps    [dstq+strideq*1], m2
    shufps                  m0, m2, m1, q3210
%if cpuflag(ssse3)
    pshufb                  m2, [pb_4_5_8to13_8x0]
%else
    pshuflw                 m2, m2, q2222
    psrldq                  m2, 6
%endif
    psrldq                  m0, 6
    movh      [dstq+strideq*2], m0
    movh      [dstq+stride3q ], m2
    RET

cglobal vp9_ipred_vr_8x8_16, 4, 4, 5, dst, stride, l, a
    movu                    m1, [aq-2]              ; *abcdefg
    movu                    m2, [lq]                ; stuvwxyz
    mova                    m0, [aq]                ; abcdefgh
    PALIGNR                 m3, m1, m2, 14, m4      ; z*abcdef
    LOWPASS                  3,  1,  0
    pavgw                   m0, m1
    PALIGNR                 m1, m2,  2, m4          ; tuvwxyz*
    pslldq                  m4, m2,  2              ; .stuvwxy
    LOWPASS                  4,  2,  1
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]

    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m3
    PALIGNR                 m0, m4, 14, m1
    pslldq                  m4, 2
    PALIGNR                 m3, m4, 14, m1
    pslldq                  m4, 2
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m3
    lea                   dstq, [dstq+strideq*4]
    PALIGNR                 m0, m4, 14, m1
    pslldq                  m4, 2
    PALIGNR                 m3, m4, 14, m1
    pslldq                  m4, 2
    mova      [dstq+strideq*0], m0
    mova      [dstq+strideq*1], m3
    PALIGNR                 m0, m4, 14, m1
    pslldq                  m4, 2
    PALIGNR                 m3, m4, 14, m4
    mova      [dstq+strideq*2], m0
    mova      [dstq+stride3q ], m3
    RET

cglobal vp9_ipred_vr_16x16_16, 4, 4, 8, dst, stride, l, a
    movu                    m1, [aq-2]              ; *abcdefg
    movu                    m2, [aq+mmsize-2]       ; hijklmno
    mova                    m3, [aq]                ; abcdefgh
    mova                    m4, [aq+mmsize]         ; ijklmnop
    mova                    m5, [lq+mmsize]         ; stuvwxyz
    PALIGNR                 m0, m1, m5, 14, m6      ; z*abcdef
    movu                    m6, [aq+mmsize-4]       ; ghijklmn
    LOWPASS                  6,  2,  4
    pavgw                   m2, m4
    LOWPASS                  0,  1,  3
    pavgw                   m3, m1
    PALIGNR                 m1, m5,  2, m7          ; tuvwxyz*
    movu                    m7, [lq+mmsize-2]       ; rstuvwxy
    LOWPASS                  1,  5,  7
    movu                    m5, [lq+2]              ; lmnopqrs
    pslldq                  m4, m5,  2              ; .lmnopqr
    pslldq                  m7, m5,  4              ; ..lmnopq
    LOWPASS                  5,  4,  7
    psrld                   m4, m1, 16
    psrld                   m7, m5, 16
    pand                    m1, [pd_65535]
    pand                    m5, [pd_65535]
    packssdw                m7, m4
    packssdw                m5, m1
    DEFINE_ARGS dst, stride, cnt
    mov                   cntd, 8

.loop:
    mova   [dstq+strideq*0+ 0], m3
    mova   [dstq+strideq*0+16], m2
    mova   [dstq+strideq*1+ 0], m0
    mova   [dstq+strideq*1+16], m6
    lea                   dstq, [dstq+strideq*2]
    PALIGNR                 m2, m3, 14, m4
    PALIGNR                 m3, m7, 14, m4
    pslldq                  m7, 2
    PALIGNR                 m6, m0, 14, m4
    PALIGNR                 m0, m5, 14, m4
    pslldq                  m5, 2
    dec                   cntd
    jg .loop
    RET

cglobal vp9_ipred_vr_32x32_16, 4, 5, 14, 6 * mmsize * ARCH_X86_32, dst, stride, l, a
    movu                    m0, [aq+mmsize*0-2]     ; *a[0-6]
    movu                    m1, [aq+mmsize*1-2]     ; a[7-14]
    movu                    m2, [aq+mmsize*2-2]     ; a[15-22]
    movu                    m3, [aq+mmsize*3-2]     ; a[23-30]
    mova                    m4, [aq+mmsize*3+0]     ; a[24-31]
    movu                    m5, [aq+mmsize*3-4]     ; a[22-29]
    LOWPASS                  5,  3,  4              ; A[23-30]
    SCRATCH                  5,  8, rsp+0*mmsize
    pavgw                   m3, m4
    mova                    m4, [aq+mmsize*2+0]     ; a[16-23]
    movu                    m6, [aq+mmsize*2-4]     ; a[14-21]
    LOWPASS                  6,  2,  4              ; A[15-22]
    SCRATCH                  6,  9, rsp+1*mmsize
    pavgw                   m2, m4
    mova                    m4, [aq+mmsize*1+0]     ; a[8-15]
    movu                    m7, [aq+mmsize*1-4]     ; a[6-13]
    LOWPASS                  7,  1,  4              ; A[7-14]
    SCRATCH                  7, 10, rsp+2*mmsize
    pavgw                   m1, m4
    mova                    m4, [aq+mmsize*0+0]     ; a[0-7]
    mova                    m5, [lq+mmsize*3+0]     ; l[24-31]
    PALIGNR                 m6, m0, m5, 14, m7      ; l[31]*a[0-5]
    LOWPASS                  6,  0,  4              ; #A[0-6]
    SCRATCH                  6, 11, rsp+3*mmsize
    pavgw                   m4, m0
    PALIGNR                 m0, m5,  2, m7          ; l[25-31]*
    movu                    m7, [lq+mmsize*3-2]     ; l[23-30]
    LOWPASS                  0,  5,  7              ; L[24-31]
    movu                    m5, [lq+mmsize*2-2]     ; l[15-22]
    mova                    m7, [lq+mmsize*2+0]     ; l[16-23]
    movu                    m6, [lq+mmsize*2+2]     ; l[17-24]
    LOWPASS                  5,  7,  6              ; L[16-23]
    psrld                   m7, m0, 16
    psrld                   m6, m5, 16
    pand                    m0, [pd_65535]
    pand                    m5, [pd_65535]
    packssdw                m6, m7
    packssdw                m5, m0
    SCRATCH                  5, 12, rsp+4*mmsize
    SCRATCH                  6, 13, rsp+5*mmsize
    movu                    m6, [lq+mmsize*1-2]     ; l[7-14]
    mova                    m0, [lq+mmsize*1+0]     ; l[8-15]
    movu                    m5, [lq+mmsize*1+2]     ; l[9-16]
    LOWPASS                  6,  0,  5              ; L[8-15]
    movu                    m0, [lq+mmsize*0+2]     ; l[1-8]
    pslldq                  m5, m0,  2              ; .l[1-7]
    pslldq                  m7, m0,  4              ; ..l[1-6]
    LOWPASS                  0,  5,  7
    psrld                   m5, m6, 16
    psrld                   m7, m0, 16
    pand                    m6, [pd_65535]
    pand                    m0, [pd_65535]
    packssdw                m7, m5
    packssdw                m0, m6
    UNSCRATCH                6, 13, rsp+5*mmsize
    DEFINE_ARGS dst, stride, stride16, cnt, stride17
    mov              stride16q, strideq
    mov                   cntd, 8
    shl              stride16q, 4
%if ARCH_X86_64
    lea              stride17q, [stride16q+strideq]
%endif

.loop:
    mova   [dstq+strideq*0+ 0], m4
    mova   [dstq+strideq*0+16], m1
    mova   [dstq+strideq*0+32], m2
    mova   [dstq+strideq*0+48], m3
%if ARCH_X86_64
    mova   [dstq+strideq*1+ 0], m11
    mova   [dstq+strideq*1+16], m10
    mova   [dstq+strideq*1+32], m9
    mova   [dstq+strideq*1+48], m8
%endif
    mova   [dstq+stride16q+ 0], m6
    mova   [dstq+stride16q+16], m4
    mova   [dstq+stride16q+32], m1
    mova   [dstq+stride16q+48], m2
%if ARCH_X86_64
    mova   [dstq+stride17q+ 0], m12
    mova   [dstq+stride17q+16], m11
    mova   [dstq+stride17q+32], m10
    mova   [dstq+stride17q+48], m9
%endif
    lea                   dstq, [dstq+strideq*2]
    PALIGNR                 m3, m2,  14, m5
    PALIGNR                 m2, m1,  14, m5
    PALIGNR                 m1, m4,  14, m5
    PALIGNR                 m4, m6,  14, m5
    PALIGNR                 m6, m7,  14, m5
    pslldq                  m7, 2
%if ARCH_X86_64
    PALIGNR                 m8, m9,  14, m5
    PALIGNR                 m9, m10, 14, m5
    PALIGNR                m10, m11, 14, m5
    PALIGNR                m11, m12, 14, m5
    PALIGNR                m12, m0,  14, m5
    pslldq                  m0, 2
%endif
    dec                   cntd
    jg .loop

%if ARCH_X86_32
    UNSCRATCH                5, 12, rsp+4*mmsize
    UNSCRATCH                4, 11, rsp+3*mmsize
    UNSCRATCH                3, 10, rsp+2*mmsize
    UNSCRATCH                2,  9, rsp+1*mmsize
    UNSCRATCH                1,  8, rsp+0*mmsize
    mov                   dstq, dstm
    mov                   cntd, 8
    add                   dstq, strideq
.loop2:
    mova   [dstq+strideq*0+ 0], m4
    mova   [dstq+strideq*0+16], m3
    mova   [dstq+strideq*0+32], m2
    mova   [dstq+strideq*0+48], m1
    mova   [dstq+stride16q+ 0], m5
    mova   [dstq+stride16q+16], m4
    mova   [dstq+stride16q+32], m3
    mova   [dstq+stride16q+48], m2
    lea                   dstq, [dstq+strideq*2]
    PALIGNR                 m1, m2,  14, m6
    PALIGNR                 m2, m3,  14, m6
    PALIGNR                 m3, m4,  14, m6
    PALIGNR                 m4, m5,  14, m6
    PALIGNR                 m5, m0,  14, m6
    pslldq                  m0, 2
    dec                   cntd
    jg .loop2
%endif
    RET
%endmacro

INIT_XMM sse2
VR_FUNCS
INIT_XMM ssse3
VR_FUNCS
INIT_XMM avx
VR_FUNCS

%macro HU_FUNCS 1 ; stack_mem_for_32x32_32bit_function
cglobal vp9_ipred_hu_4x4_16, 3, 3, 3, dst, stride, l, a
    movh                    m0, [lq]                ; abcd
%if cpuflag(ssse3)
    pshufb                  m0, [pb_0to7_67x4]      ; abcddddd
%else
    punpcklqdq              m0, m0
    pshufhw                 m0, m0, q3333           ; abcddddd
%endif
    psrldq                  m1, m0,  2              ; bcddddd.
    psrldq                  m2, m0,  4              ; cddddd..
    LOWPASS                  2,  1,  0              ; BCDddd..
    pavgw                   m1, m0                  ; abcddddd
    SBUTTERFLY          wd,  1,  2,  0              ; aBbCcDdd, dddddddd
    PALIGNR                 m2, m1,  4, m0          ; bCcDdddd
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]

    movh      [dstq+strideq*0], m1                  ; aBbC
    movh      [dstq+strideq*1], m2                  ; bCcD
    movhps    [dstq+strideq*2], m1                  ; cDdd
    movhps    [dstq+stride3q ], m2                  ; dddd
    RET

cglobal vp9_ipred_hu_8x8_16, 3, 3, 4, dst, stride, l, a
    mova                    m0, [lq]
%if cpuflag(ssse3)
    mova                    m3, [pb_2to15_14_15]
%endif
    SHIFT_RIGHTx2           m1, m2, m0, m3
    LOWPASS                  2,  1,  0
    pavgw                   m1, m0
    SBUTTERFLY          wd,  1,  2,  0
    shufps                  m0, m1, m2, q1032
    pshufd                  m3, m2, q3332
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]

    mova     [dstq+strideq *0], m1
    mova     [dstq+strideq *2], m0
    mova     [dstq+strideq *4], m2
    mova     [dstq+stride3q*2], m3
    add                   dstq, strideq
%if cpuflag(avx)
    vpalignr                m1, m2, m1, 4
%else
    PALIGNR                 m0, m2, m1, 4, m3
    mova                    m1, m0
%endif
    pshufd                  m2, m2, q3321
    shufps                  m0, m1, m2, q1032
    pshufd                  m3, m2, q3332
    mova     [dstq+strideq *0], m1
    mova     [dstq+strideq *2], m0
    mova     [dstq+strideq *4], m2
    mova     [dstq+stride3q*2], m3
    RET

cglobal vp9_ipred_hu_16x16_16, 3, 4, 6 + notcpuflag(ssse3), dst, stride, l, a
    mova                    m0, [lq]
    mova                    m3, [lq+mmsize]
    movu                    m1, [lq+2]
    movu                    m2, [lq+4]
    LOWPASS                  2,  1,  0
    pavgw                   m1, m0
    SBUTTERFLY           wd, 1,  2,  0
%if cpuflag(ssse3)
    mova                    m5, [pb_2to15_14_15]
%endif
    SHIFT_RIGHTx2           m0, m4, m3, m5
    LOWPASS                  4,  0,  3
    pavgw                   m3, m0
    SBUTTERFLY           wd, 3,  4,  5
    pshufd                  m0, m0, q3333
    DEFINE_ARGS dst, stride, stride3, cnt
    lea               stride3q, [strideq*3]
    mov                   cntd, 4

.loop:
    mova  [dstq+strideq *0+ 0], m1
    mova  [dstq+strideq *0+16], m2
    mova  [dstq+strideq *4+ 0], m2
    mova  [dstq+strideq *4+16], m3
    mova  [dstq+strideq *8+ 0], m3
    mova  [dstq+strideq *8+16], m4
    mova  [dstq+stride3q*4+ 0], m4
    mova  [dstq+stride3q*4+16], m0
    add                   dstq, strideq
%if cpuflag(avx)
    vpalignr                m1, m2, m1, 4
    vpalignr                m2, m3, m2, 4
    vpalignr                m3, m4, m3, 4
    vpalignr                m4, m0, m4, 4
%else
    PALIGNR                 m5, m2, m1, 4, m6
    mova                    m1, m5
    PALIGNR                 m5, m3, m2, 4, m6
    mova                    m2, m5
    PALIGNR                 m5, m4, m3, 4, m6
    mova                    m3, m5
    PALIGNR                 m5, m0, m4, 4, m6
    mova                    m4, m5
%endif
    dec                   cntd
    jg .loop
    RET

cglobal vp9_ipred_hu_32x32_16, 3, 7, 10 + notcpuflag(ssse3), \
                               %1 * -mmsize * ARCH_X86_32, dst, stride, l, a
    mova                    m2, [lq+mmsize*0+0]
    movu                    m1, [lq+mmsize*0+2]
    movu                    m0, [lq+mmsize*0+4]
    LOWPASS                  0,  1,  2
    pavgw                   m1, m2
    SBUTTERFLY           wd, 1,  0,  2
    SCRATCH                  1,  8, rsp+0*mmsize
    mova                    m4, [lq+mmsize*1+0]
    movu                    m3, [lq+mmsize*1+2]
    movu                    m2, [lq+mmsize*1+4]
    LOWPASS                  2,  3,  4
    pavgw                   m3, m4
    SBUTTERFLY           wd, 3,  2,  4
    mova                    m6, [lq+mmsize*2+0]
    movu                    m5, [lq+mmsize*2+2]
    movu                    m4, [lq+mmsize*2+4]
    LOWPASS                  4,  5,  6
    pavgw                   m5, m6
    SBUTTERFLY           wd, 5,  4,  6
    mova                    m7, [lq+mmsize*3+0]
    SCRATCH                  0,  9, rsp+1*mmsize
%if cpuflag(ssse3)
    mova                    m0, [pb_2to15_14_15]
%endif
    SHIFT_RIGHTx2           m1, m6, m7, m0
    LOWPASS                  6,  1,  7
    pavgw                   m7, m1
    SBUTTERFLY           wd, 7,  6,  0
    pshufd                  m1, m1, q3333
    UNSCRATCH                0,  9, rsp+1*mmsize
    DEFINE_ARGS dst, stride, cnt, stride3, stride4, stride20, stride28
    lea               stride3q, [strideq*3]
    lea               stride4q, [strideq*4]
    lea              stride28q, [stride4q*8]
    lea              stride20q, [stride4q*5]
    sub              stride28q, stride4q
    mov                   cntd, 4

.loop:
%if ARCH_X86_64
    SWAP                     1,  8
%else
    mova        [rsp+1*mmsize], m1
    mova                    m1, [rsp+0*mmsize]
%endif
    mova  [dstq+strideq *0+ 0], m1
    mova  [dstq+strideq *0+16], m0
    mova  [dstq+strideq *0+32], m3
    mova  [dstq+strideq *0+48], m2
    mova  [dstq+stride4q*1+ 0], m0
    mova  [dstq+stride4q*1+16], m3
    mova  [dstq+stride4q*1+32], m2
    mova  [dstq+stride4q*1+48], m5
    mova  [dstq+stride4q*2+ 0], m3
    mova  [dstq+stride4q*2+16], m2
    mova  [dstq+stride4q*2+32], m5
    mova  [dstq+stride4q*2+48], m4
%if cpuflag(avx)
    vpalignr                m1, m0, m1, 4
    vpalignr                m0, m3, m0, 4
    vpalignr                m3, m2, m3, 4
%else
    SCRATCH                  6,  9, rsp+2*mmsize
%if notcpuflag(ssse3)
    SCRATCH                  7, 10, rsp+3*mmsize
%endif
    PALIGNR                 m6, m0, m1, 4, m7
    mova                    m1, m6
    PALIGNR                 m6, m3, m0, 4, m7
    mova                    m0, m6
    PALIGNR                 m6, m2, m3, 4, m7
    mova                    m3, m6
    UNSCRATCH                6,  9, rsp+2*mmsize
    SCRATCH                  0,  9, rsp+2*mmsize
%if notcpuflag(ssse3)
    UNSCRATCH                7, 10, rsp+3*mmsize
    SCRATCH                  3, 10, rsp+3*mmsize
%endif
%endif
%if ARCH_X86_64
    SWAP                     1,  8
%else
    mova        [rsp+0*mmsize], m1
    mova                    m1, [rsp+1*mmsize]
%endif
    mova  [dstq+stride3q*4+ 0], m2
    mova  [dstq+stride3q*4+16], m5
    mova  [dstq+stride3q*4+32], m4
    mova  [dstq+stride3q*4+48], m7
    mova  [dstq+stride4q*4+ 0], m5
    mova  [dstq+stride4q*4+16], m4
    mova  [dstq+stride4q*4+32], m7
    mova  [dstq+stride4q*4+48], m6
    mova  [dstq+stride20q + 0], m4
    mova  [dstq+stride20q +16], m7
    mova  [dstq+stride20q +32], m6
    mova  [dstq+stride20q +48], m1
    mova  [dstq+stride3q*8+ 0], m7
    mova  [dstq+stride3q*8+16], m6
    mova  [dstq+stride3q*8+32], m1
    mova  [dstq+stride3q*8+48], m1
    mova  [dstq+stride28q + 0], m6
    mova  [dstq+stride28q +16], m1
    mova  [dstq+stride28q +32], m1
    mova  [dstq+stride28q +48], m1
%if cpuflag(avx)
    vpalignr                m2, m5, m2, 4
    vpalignr                m5, m4, m5, 4
    vpalignr                m4, m7, m4, 4
    vpalignr                m7, m6, m7, 4
    vpalignr                m6, m1, m6, 4
%else
    PALIGNR                 m0, m5, m2, 4, m3
    mova                    m2, m0
    PALIGNR                 m0, m4, m5, 4, m3
    mova                    m5, m0
    PALIGNR                 m0, m7, m4, 4, m3
    mova                    m4, m0
    PALIGNR                 m0, m6, m7, 4, m3
    mova                    m7, m0
    PALIGNR                 m0, m1, m6, 4, m3
    mova                    m6, m0
    UNSCRATCH                0,  9, rsp+2*mmsize
%if notcpuflag(ssse3)
    UNSCRATCH                3, 10, rsp+3*mmsize
%endif
%endif
    add                   dstq, strideq
    dec                   cntd
    jg .loop
    RET
%endmacro

INIT_XMM sse2
HU_FUNCS 4
INIT_XMM ssse3
HU_FUNCS 3
INIT_XMM avx
HU_FUNCS 2

%macro HD_FUNCS 0
cglobal vp9_ipred_hd_4x4_16, 4, 4, 4, dst, stride, l, a
    movh                    m0, [lq]
    movhps                  m0, [aq-2]
    psrldq                  m1, m0, 2
    psrldq                  m2, m0, 4
    LOWPASS                  2,  1,  0
    pavgw                   m1, m0
    punpcklwd               m1, m2
    DEFINE_ARGS dst, stride, stride3
    lea               stride3q, [strideq*3]

    movh      [dstq+stride3q ], m1
    movhps    [dstq+strideq*1], m1
    movhlps                 m2, m2
    PALIGNR                 m2, m1, 4, m0
    movh      [dstq+strideq*2], m2
    movhps    [dstq+strideq*0], m2
    RET

cglobal vp9_ipred_hd_8x8_16, 4, 4, 5, dst, stride, l, a
    mova                    m0, [lq]
    movu                    m1, [aq-2]
    PALIGNR                 m2, m1, m0, 2, m3
    PALIGNR                 m3, m1, m0, 4, m4
    LOWPASS                  3,  2,  0
    pavgw                   m2, m0
    SBUTTERFLY           wd, 2,  3,  0
    psrldq                  m0, m1,  2
    psrldq                  m4, m1,  4
    LOWPASS                  1,  0,  4
    DEFINE_ARGS dst8, mstride, cnt
    lea                  dst8q, [dst8q+mstrideq*8]
    neg               mstrideq
    mov                   cntd, 4

.loop:
    add                  dst8q, mstrideq
    mova    [dst8q+mstrideq*0], m2
    mova    [dst8q+mstrideq*4], m3
%if cpuflag(avx)
    vpalignr                m2, m3, m2, 4
    vpalignr                m3, m1, m3, 4
%else
    PALIGNR                 m0, m3, m2, 4, m4
    mova                    m2, m0
    PALIGNR                 m0, m1, m3, 4, m4
    mova                    m3, m0
%endif
    psrldq                  m1, 4
    dec                   cntd
    jg .loop
    RET

cglobal vp9_ipred_hd_16x16_16, 4, 4, 8, dst, stride, l, a
    mova                    m2, [lq]
    movu                    m1, [lq+2]
    movu                    m0, [lq+4]
    LOWPASS                  0,  1,  2
    pavgw                   m1, m2
    mova                    m4, [lq+mmsize]
    movu                    m5, [aq-2]
    PALIGNR                 m3, m5, m4, 2, m6
    PALIGNR                 m2, m5, m4, 4, m6
    LOWPASS                  2,  3,  4
    pavgw                   m3, m4
    SBUTTERFLY           wd, 1,  0,  4
    SBUTTERFLY           wd, 3,  2,  4
    mova                    m6, [aq]
    movu                    m4, [aq+2]
    LOWPASS                  4,  6,  5
    movu                    m5, [aq+mmsize-2]
    psrldq                  m6, m5,  2
    psrldq                  m7, m5,  4
    LOWPASS                  5,  6,  7
    DEFINE_ARGS dst, mstride, mstride3, cnt
    lea                   dstq, [dstq+mstrideq*8]
    lea                   dstq, [dstq+mstrideq*8]
    neg               mstrideq
    lea              mstride3q, [mstrideq*3]
    mov                   cntd, 4

.loop:
    add                  dstq, mstrideq
    mova [dstq+mstride3q*4+ 0], m2
    mova [dstq+mstride3q*4+16], m4
    mova [dstq+mstrideq *8+ 0], m3
    mova [dstq+mstrideq *8+16], m2
    mova [dstq+mstrideq *4+ 0], m0
    mova [dstq+mstrideq *4+16], m3
    mova [dstq+mstrideq *0+ 0], m1
    mova [dstq+mstrideq *0+16], m0
%if cpuflag(avx)
    vpalignr                m1, m0, m1, 4
    vpalignr                m0, m3, m0, 4
    vpalignr                m3, m2, m3, 4
    vpalignr                m2, m4, m2, 4
    vpalignr                m4, m5, m4, 4
%else
    PALIGNR                 m6, m0, m1, 4, m7
    mova                    m1, m6
    PALIGNR                 m6, m3, m0, 4, m7
    mova                    m0, m6
    PALIGNR                 m6, m2, m3, 4, m7
    mova                    m3, m6
    PALIGNR                 m6, m4, m2, 4, m7
    mova                    m2, m6
    PALIGNR                 m6, m5, m4, 4, m7
    mova                    m4, m6
%endif
    psrldq                  m5, 4
    dec                   cntd
    jg .loop
    RET

cglobal vp9_ipred_hd_32x32_16, 4, 4 + 3 * ARCH_X86_64, 14, \
                               10 * -mmsize * ARCH_X86_32, dst, stride, l, a
    mova                    m2, [lq+mmsize*0+0]
    movu                    m1, [lq+mmsize*0+2]
    movu                    m0, [lq+mmsize*0+4]
    LOWPASS                  0,  1,  2
    pavgw                   m1, m2
    SBUTTERFLY           wd, 1,  0,  2
    mova                    m4, [lq+mmsize*1+0]
    movu                    m3, [lq+mmsize*1+2]
    movu                    m2, [lq+mmsize*1+4]
    LOWPASS                  2,  3,  4
    pavgw                   m3, m4
    SBUTTERFLY           wd, 3,  2,  4
    SCRATCH                  0,  8, rsp+0*mmsize
    SCRATCH                  1,  9, rsp+1*mmsize
    SCRATCH                  2, 10, rsp+2*mmsize
    SCRATCH                  3, 11, rsp+3*mmsize
    mova                    m6, [lq+mmsize*2+0]
    movu                    m5, [lq+mmsize*2+2]
    movu                    m4, [lq+mmsize*2+4]
    LOWPASS                  4,  5,  6
    pavgw                   m5, m6
    SBUTTERFLY           wd, 5,  4,  6
    mova                    m0, [lq+mmsize*3+0]
    movu                    m1, [aq+mmsize*0-2]
    PALIGNR                 m7, m1, m0, 2, m2
    PALIGNR                 m6, m1, m0, 4, m2
    LOWPASS                  6,  7,  0
    pavgw                   m7, m0
    SBUTTERFLY           wd, 7,  6,  0
    mova                    m2, [aq+mmsize*0+0]
    movu                    m0, [aq+mmsize*0+2]
    LOWPASS                  0,  2,  1
    movu                    m1, [aq+mmsize*1-2]
    mova                    m2, [aq+mmsize*1+0]
    movu                    m3, [aq+mmsize*1+2]
    LOWPASS                  1,  2,  3
    SCRATCH                  6, 12, rsp+6*mmsize
    SCRATCH                  7, 13, rsp+7*mmsize
    movu                    m2, [aq+mmsize*2-2]
    mova                    m3, [aq+mmsize*2+0]
    movu                    m6, [aq+mmsize*2+2]
    LOWPASS                  2,  3,  6
    movu                    m3, [aq+mmsize*3-2]
    psrldq                  m6, m3,  2
    psrldq                  m7, m3,  4
    LOWPASS                  3,  6,  7
    UNSCRATCH                6, 12, rsp+6*mmsize
    UNSCRATCH                7, 13, rsp+7*mmsize
%if ARCH_X86_32
    mova        [rsp+4*mmsize], m4
    mova        [rsp+5*mmsize], m5
    ; we already backed up m6/m7 earlier on x86-32 in SCRATCH, so we don't need
    ; to do it again here
%endif
    DEFINE_ARGS dst, stride, cnt, stride3, stride4, stride20, stride28
    mov                   cntd, 4
    lea               stride3q, [strideq*3]
%if ARCH_X86_64
    lea               stride4q, [strideq*4]
    lea              stride28q, [stride4q*8]
    lea              stride20q, [stride4q*5]
    sub              stride28q, stride4q
%endif
    add                   dstq, stride3q

    ; x86-32 doesn't have enough registers, so on that platform, we split
    ; the loop in 2... Otherwise you spend most of the loop (un)scratching
.loop:
%if ARCH_X86_64
    mova  [dstq+stride28q + 0], m9
    mova  [dstq+stride28q +16], m8
    mova  [dstq+stride28q +32], m11
    mova  [dstq+stride28q +48], m10
    mova  [dstq+stride3q*8+ 0], m8
    mova  [dstq+stride3q*8+16], m11
    mova  [dstq+stride3q*8+32], m10
    mova  [dstq+stride3q*8+48], m5
    mova  [dstq+stride20q + 0], m11
    mova  [dstq+stride20q +16], m10
    mova  [dstq+stride20q +32], m5
    mova  [dstq+stride20q +48], m4
    mova  [dstq+stride4q*4+ 0], m10
    mova  [dstq+stride4q*4+16], m5
    mova  [dstq+stride4q*4+32], m4
    mova  [dstq+stride4q*4+48], m7
%endif
    mova  [dstq+stride3q*4+ 0], m5
    mova  [dstq+stride3q*4+16], m4
    mova  [dstq+stride3q*4+32], m7
    mova  [dstq+stride3q*4+48], m6
    mova  [dstq+strideq* 8+ 0], m4
    mova  [dstq+strideq* 8+16], m7
    mova  [dstq+strideq* 8+32], m6
    mova  [dstq+strideq* 8+48], m0
    mova  [dstq+strideq* 4+ 0], m7
    mova  [dstq+strideq* 4+16], m6
    mova  [dstq+strideq* 4+32], m0
    mova  [dstq+strideq* 4+48], m1
    mova  [dstq+strideq* 0+ 0], m6
    mova  [dstq+strideq* 0+16], m0
    mova  [dstq+strideq* 0+32], m1
    mova  [dstq+strideq* 0+48], m2
    sub                   dstq, strideq
%if cpuflag(avx)
%if ARCH_X86_64
    vpalignr                m9, m8,  m9,  4
    vpalignr                m8, m11, m8,  4
    vpalignr               m11, m10, m11, 4
    vpalignr               m10, m5,  m10, 4
%endif
    vpalignr                m5, m4,  m5,  4
    vpalignr                m4, m7,  m4,  4
    vpalignr                m7, m6,  m7,  4
    vpalignr                m6, m0,  m6,  4
    vpalignr                m0, m1,  m0,  4
    vpalignr                m1, m2,  m1,  4
    vpalignr                m2, m3,  m2,  4
%else
%if ARCH_X86_64
    PALIGNR                m12, m8,  m9,  4, m13
    mova                    m9, m12
    PALIGNR                m12, m11, m8,  4, m13
    mova                    m8, m12
    PALIGNR                m12, m10, m11, 4, m13
    mova                   m11, m12
    PALIGNR                m12, m5,  m10, 4, m13
    mova                   m10, m12
%endif
    SCRATCH                  3, 12, rsp+8*mmsize, sh
%if notcpuflag(ssse3)
    SCRATCH                  2, 13, rsp+9*mmsize
%endif
    PALIGNR                 m3, m4,  m5,  4, m2
    mova                    m5, m3
    PALIGNR                 m3, m7,  m4,  4, m2
    mova                    m4, m3
    PALIGNR                 m3, m6,  m7,  4, m2
    mova                    m7, m3
    PALIGNR                 m3, m0,  m6,  4, m2
    mova                    m6, m3
    PALIGNR                 m3, m1,  m0,  4, m2
    mova                    m0, m3
%if notcpuflag(ssse3)
    UNSCRATCH                2, 13, rsp+9*mmsize
    SCRATCH                  0, 13, rsp+9*mmsize
%endif
    PALIGNR                 m3, m2,  m1,  4, m0
    mova                    m1, m3
    PALIGNR                 m3, reg_sh,  m2,  4, m0
    mova                    m2, m3
%if notcpuflag(ssse3)
    UNSCRATCH                0, 13, rsp+9*mmsize
%endif
    UNSCRATCH                3, 12, rsp+8*mmsize, sh
%endif
    psrldq                  m3, 4
    dec                   cntd
    jg .loop

%if ARCH_X86_32
    UNSCRATCH                0,  8, rsp+0*mmsize
    UNSCRATCH                1,  9, rsp+1*mmsize
    UNSCRATCH                2, 10, rsp+2*mmsize
    UNSCRATCH                3, 11, rsp+3*mmsize
    mova                    m4, [rsp+4*mmsize]
    mova                    m5, [rsp+5*mmsize]
    mova                    m6, [rsp+6*mmsize]
    mova                    m7, [rsp+7*mmsize]
    DEFINE_ARGS dst, stride, stride5, stride3
    lea               stride5q, [strideq*5]
    lea                   dstq, [dstq+stride5q*4]
    DEFINE_ARGS dst, stride, cnt, stride3
    mov                   cntd, 4
.loop_2:
    mova  [dstq+stride3q*4+ 0], m1
    mova  [dstq+stride3q*4+16], m0
    mova  [dstq+stride3q*4+32], m3
    mova  [dstq+stride3q*4+48], m2
    mova  [dstq+strideq* 8+ 0], m0
    mova  [dstq+strideq* 8+16], m3
    mova  [dstq+strideq* 8+32], m2
    mova  [dstq+strideq* 8+48], m5
    mova  [dstq+strideq* 4+ 0], m3
    mova  [dstq+strideq* 4+16], m2
    mova  [dstq+strideq* 4+32], m5
    mova  [dstq+strideq* 4+48], m4
    mova  [dstq+strideq* 0+ 0], m2
    mova  [dstq+strideq* 0+16], m5
    mova  [dstq+strideq* 0+32], m4
    mova  [dstq+strideq* 0+48], m7
    sub                   dstq, strideq
%if cpuflag(avx)
    vpalignr                m1, m0,  m1,  4
    vpalignr                m0, m3,  m0,  4
    vpalignr                m3, m2,  m3,  4
    vpalignr                m2, m5,  m2,  4
    vpalignr                m5, m4,  m5,  4
    vpalignr                m4, m7,  m4,  4
    vpalignr                m7, m6,  m7,  4
%else
    SCRATCH                  6, 12, rsp+8*mmsize, sh
%if notcpuflag(ssse3)
    SCRATCH                  7, 13, rsp+9*mmsize
%endif
    PALIGNR                 m6, m0,  m1,  4, m7
    mova                    m1, m6
    PALIGNR                 m6, m3,  m0,  4, m7
    mova                    m0, m6
    PALIGNR                 m6, m2,  m3,  4, m7
    mova                    m3, m6
    PALIGNR                 m6, m5,  m2,  4, m7
    mova                    m2, m6
    PALIGNR                 m6, m4,  m5,  4, m7
    mova                    m5, m6
%if notcpuflag(ssse3)
    UNSCRATCH                7, 13, rsp+9*mmsize
    SCRATCH                  5, 13, rsp+9*mmsize
%endif
    PALIGNR                 m6, m7,  m4,  4, m5
    mova                    m4, m6
    PALIGNR                 m6, reg_sh,  m7,  4, m5
    mova                    m7, m6
%if notcpuflag(ssse3)
    UNSCRATCH                5, 13, rsp+9*mmsize
%endif
    UNSCRATCH                6, 12, rsp+8*mmsize, sh
%endif
    psrldq                  m6, 4
    dec                   cntd
    jg .loop_2
%endif
    RET
%endmacro

INIT_XMM sse2
HD_FUNCS
INIT_XMM ssse3
HD_FUNCS
INIT_XMM avx
HD_FUNCS
