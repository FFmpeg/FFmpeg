;******************************************************************************
;* mpeg4 qpel
;* Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
;* Copyright (c) 2008 Loren Merritt
;* Copyright (c) 2013 Daniel Kang
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

cextern pw_3
pw_15: times 8 dw 15
cextern pw_16
pw_20: times 8 dw 20

coeff8_0: times 16 db 20  ; pb_20
coeff8_1: db -6,  3, -6,  3,  3, -6,  3, -6, -6,  3, -6,  3,  3, -6,  3, -6
coeff8_2: db -6,  3, -1,  0, -6,  3, -6,  3,  3, -6,  3, -6,  0, -1,  3, -6
coeff8_3: db -1, -1, -3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -3, -1, -1
shuffle_mask8: db 3, 7, 0, 9, 0, 12, 0, 14, 1, 15, 3, 15, 6, 15, 8, 12

shuffle_mask16_0: db 2, 1, 1, 0, 0, 0, 1, 0, 1,  2,  2,  3,  4,  3,  5,  4
shuffle_mask16_1: db 5, 6, 6, 7, 8, 7, 9, 8, 9, 10, 10, 11, 12, 11, 13, 12
shuffle_mask16_2: db 0, 1, 1, 2, 3, 2, 3, 3,  3, 2,  2,  1, -1, -1, -1, -1
coeff16_0: times 2 db -1,  3, -1,  3,  3, -1,  3, -1
coeff16_1: times 2 db 20, -6, 20, -6, -6, 20, -6, 20

SECTION .text

%macro PUT_NO_RND_PIXELS_L2 1
; void ff_put_no_rnd_pixels8x8_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
;                                 ptrdiff_t dstStride, ptrdiff_t src1Stride)
cglobal put_no_rnd_pixels%1x%1_l2, 5,6,5
    pcmpeqb      m4, m4
    mov         r5d, %1
.loop:
    movu         m0, [r1]
    add          r1, r4
    movu         m1, [r1]
    add          r1, r4
    mova         m2, [r2]
    mova         m3, [r2+%1]
    pxor         m0, m4
    pxor         m1, m4
    pxor         m2, m4
    pxor         m3, m4
    pavgb        m0, m2
    pavgb        m1, m3
    pxor         m0, m4
    pxor         m1, m4
    mova       [r0], m0
    add          r0, r3
    mova       [r0], m1
    add          r0, r3
    movu         m0, [r1]
    add          r1, r4
    movu         m1, [r1]
    add          r1, r4
    mova         m2, [r2+2*%1]
    mova         m3, [r2+3*%1]
    add          r2, 4*%1
    pxor         m0, m4
    pxor         m1, m4
    pxor         m2, m4
    pxor         m3, m4
    pavgb        m0, m2
    pavgb        m1, m3
    pxor         m0, m4
    pxor         m1, m4
    mova       [r0], m0
    add          r0, r3
    mova       [r0], m1
    add          r0, r3
    sub         r5d, 4
    jne .loop
    RET
%endmacro

INIT_MMX mmxext
PUT_NO_RND_PIXELS_L2 8
INIT_XMM sse2
PUT_NO_RND_PIXELS_L2 16

%macro L2 5
%ifidn %2, l2
%ifidn %1, put_no_rnd
%ifn UNIX64
    pcmpeqb      %5, %5
%endif
    pxor         %4, PW_FF
    pxor         %3, PW_FF
    pavgb        %3, %4
    pxor         %3, PW_FF
%else ; avg or put
    pavgb        %3, %4
%endif
%endif
%endmacro

%macro MPEG4_QPEL16_H_LOWPASS 1-2 ""
%ifidn %2, l2
cglobal mpeg4_%1_qpel16_h_lowpass_l2, 6, 6, 8+UNIX64, dst, src, dstride, srcstride, h, offset
%else
cglobal mpeg4_%1_qpel16_h_lowpass, 5, 5, 8, dst, src, dstride, srcstride, h
%endif
    mova         m7, [coeff16_0]
%define PW_FF m1
%ifidn %1, put_no_rnd
%ifidn %2, l2
%if UNIX64
    pcmpeqb      m8, m8
%define PW_FF m8
%endif
%endif
%endif
.loop:
    movu         m0, [srcq]
    pshufb       m1, m0, [shuffle_mask16_0]
    pmaddubsw    m2, m1, m7
    pshufb       m0, [shuffle_mask16_1]
    pmaddubsw    m4, m0, m7
    palignr      m3, m0, m1, 4
    pmaddubsw    m3, [coeff16_1]
    palignr      m5, m0, m1, 8
    movd         m6, [srcq+13]
    pmaddubsw    m5, [coeff16_1]
    paddw        m2, m3
    palignr      m3, m0, m1, 12
    pshufb       m6, [shuffle_mask16_2]
    pmaddubsw    m3, m7
    paddw        m4, [PW_ROUND]
    palignr      m1, m6, m0, 4
    pmaddubsw    m1, [coeff16_1]
    paddw        m2, m5
    palignr      m5, m6, m0, 8
    pmaddubsw    m5, [coeff16_1]
    palignr      m6, m0, 12
%ifidn %2, l2
    movu         m0, [srcq+offsetq]
%endif
    pmaddubsw    m6, m7
    add        srcq, srcstrideq
    paddw        m2, [PW_ROUND]
    paddw        m4, m1
    paddw        m2, m3
    paddw        m4, m5
    psraw        m2, 5
    paddw        m4, m6
    psraw        m4, 5
    packuswb     m2, m4
    L2           %1, %2, m2, m0, m1
%ifidn %1, avg
    pavgb        m2, [dstq]
%endif
    mova     [dstq], m2
    add        dstq, dstrideq
    dec          hd
    jne .loop
    RET
%endmacro

INIT_XMM ssse3
%define PW_ROUND pw_16
MPEG4_QPEL16_H_LOWPASS put
MPEG4_QPEL16_H_LOWPASS put, l2
%define PW_ROUND pw_16
MPEG4_QPEL16_H_LOWPASS avg
MPEG4_QPEL16_H_LOWPASS avg, l2
%define PW_ROUND pw_15
MPEG4_QPEL16_H_LOWPASS put_no_rnd
MPEG4_QPEL16_H_LOWPASS put_no_rnd, l2

%macro MPEG4_QPEL8_H_LOWPASS 1-2 ""
%ifidn %2, l2
cglobal mpeg4_%1_qpel8_h_lowpass_l2, 6, 6, 8+2*ARCH_X86_64+UNIX64, dst, src, dstride, srcstride, h, offset
%else
cglobal mpeg4_%1_qpel8_h_lowpass, 5, 5, 8+2*ARCH_X86_64, dst, src, dstride, srcstride, h
%endif
    mova         m4, [PW_ROUND]
    mova         m5, [coeff8_0]
%if ARCH_X86_64
    mova         m8, [coeff8_1]
    mova         m9, [coeff8_2]
%endif
%define PW_FF m0
%ifidn %1, put_no_rnd
%ifidn %2, l2
%if UNIX64
    pcmpeqb     m10, m10
%define PW_FF m10
%endif
%endif
%endif
    mova         m6, [coeff8_3]
    mova         m7, [shuffle_mask8]
.loop:
    movq         m0, [srcq]
    movq         m1, [srcq+1]
    punpcklbw    m0, m1
    pmaddubsw    m1, m0, m5
    pshufd       m2, m0, q2301
%if ARCH_X86_64
    pmaddubsw    m2, m8
    pshufd       m3, m0, q3120
    pmaddubsw    m3, m9
%else
    pmaddubsw    m2, [coeff8_1]
    pshufd       m3, m0, q3120
    pmaddubsw    m3, [coeff8_2]
%endif
    paddw        m1, m2
%ifidn %1, avg
    movq         m2, [dstq]
%endif
    pshufb       m0, m7
    pmaddubsw    m0, m6
    paddw        m1, m4
    paddw        m1, m3
%ifidn %2, l2
    movq         m3, [srcq+offsetq]
%endif
    add        srcq, srcstrideq
    paddw        m1, m0
    psraw        m1, 5
    packuswb     m1, m1
    L2           %1, %2, m1, m3, m0
%ifidn %1, avg
    pavgb        m1, m2
%endif
    movq     [dstq], m1
    add        dstq, dstrideq
    dec          hd
    jne .loop
    RET
%endmacro

INIT_XMM ssse3
%define PW_ROUND pw_16
MPEG4_QPEL8_H_LOWPASS put
MPEG4_QPEL8_H_LOWPASS put, l2
%define PW_ROUND pw_16
MPEG4_QPEL8_H_LOWPASS avg
MPEG4_QPEL8_H_LOWPASS avg, l2
%define PW_ROUND pw_15
MPEG4_QPEL8_H_LOWPASS put_no_rnd
MPEG4_QPEL8_H_LOWPASS put_no_rnd, l2


%macro QPEL_V_LOW 5
    paddw      m0, m1
    mova       m4, [pw_20]
    pmullw     m4, m0
    mova       m0, %4
    mova       m5, %1
    paddw      m5, m0
    psubw      m4, m5
    mova       m5, %2
    mova       m6, %3
    paddw      m5, m3
    paddw      m6, m2
    paddw      m6, m6
    psubw      m5, m6
    pmullw     m5, [pw_3]
    paddw      m4, [PW_ROUND]
    paddw      m5, m4
    psraw      m5, 5
    packuswb   m5, m5
    OP_MOV     %5, m5, m4
    SWAP 0,1,2,3
%endmacro

%macro MPEG4_QPEL16_V_LOWPASS 1
cglobal mpeg4_%1_qpel16_v_lowpass, 4, 6, 7, 544
    mov         r4d, 17
    mov          r5, rsp
    pxor         m4, m4
.looph:
    movu         m0, [r1]
    mova         m1, m0
    punpcklbw    m0, m4
    punpckhbw    m1, m4
    mova       [r5], m0
    mova [r5+0x110], m1
    add          r1, r3
    add          r5, mmsize
    dec r4d
    jne .looph


    mov         r4d, 16/(mmsize/2)
    mov          r1, r0
    mov          r5, rsp
.loopv:
    mova         m0, [r5+0 * mmsize]
    mova         m1, [r5+1 * mmsize]
    mova         m2, [r5+2 * mmsize]
    mova         m3, [r5+3 * mmsize]
    add          r1, mmsize/2
    QPEL_V_LOW [r5+2*mmsize],  [r5+1*mmsize],  [r5+0*mmsize],  [r5+4*mmsize],  [r0]
    QPEL_V_LOW [r5+1*mmsize],  [r5+0*mmsize],  [r5+0*mmsize],  [r5+5*mmsize],  [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+0*mmsize],  [r5+0*mmsize],  [r5+1*mmsize],  [r5+6*mmsize],  [r0]
    QPEL_V_LOW [r5+0*mmsize],  [r5+1*mmsize],  [r5+2*mmsize],  [r5+7*mmsize],  [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+1*mmsize],  [r5+2*mmsize],  [r5+3*mmsize],  [r5+8*mmsize],  [r0]
    QPEL_V_LOW [r5+2*mmsize],  [r5+3*mmsize],  [r5+4*mmsize],  [r5+9*mmsize],  [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+3*mmsize],  [r5+4*mmsize],  [r5+5*mmsize],  [r5+10*mmsize], [r0]
    QPEL_V_LOW [r5+4*mmsize],  [r5+5*mmsize],  [r5+6*mmsize],  [r5+11*mmsize], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+5*mmsize],  [r5+6*mmsize],  [r5+7*mmsize],  [r5+12*mmsize], [r0]
    QPEL_V_LOW [r5+6*mmsize],  [r5+7*mmsize],  [r5+8*mmsize],  [r5+13*mmsize], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+7*mmsize],  [r5+8*mmsize],  [r5+ 9*mmsize], [r5+14*mmsize], [r0]
    QPEL_V_LOW [r5+8*mmsize],  [r5+9*mmsize],  [r5+10*mmsize], [r5+15*mmsize], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+ 9*mmsize], [r5+10*mmsize], [r5+11*mmsize], [r5+16*mmsize], [r0]
    QPEL_V_LOW [r5+10*mmsize], [r5+11*mmsize], [r5+12*mmsize], [r5+16*mmsize], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+11*mmsize], [r5+12*mmsize], [r5+13*mmsize], [r5+15*mmsize], [r0]
    QPEL_V_LOW [r5+12*mmsize], [r5+13*mmsize], [r5+14*mmsize], [r5+14*mmsize], [r0+r2]

    add    r5, 17*mmsize
    mov    r0, r1
    dec r4d
    jne .loopv
    RET
%endmacro

%macro PUT_OPH 2-3
    movh %1, %2
%endmacro

%macro AVG_OPH 2-3
    movh  %3, %1
    pavgb %2, %3
    movh  %1, %2
%endmacro

%macro MPEG4_QPEL8_V_LOWPASS 1
cglobal mpeg4_%1_qpel8_v_lowpass, 4, 6, 7, 144
    mov         r4d, 9
    mov          r5, rsp
    pxor         m2, m2
.looph:
    movq         m0, [r1]
    add          r1, r3
    punpcklbw    m0, m2
    mova       [r5], m0
    add          r5, mmsize
    dec r4d
    jne .looph


%define R5 rsp
    mova         m0, [R5+0 * mmsize]
    mova         m1, [R5+1 * mmsize]
    mova         m2, [R5+2 * mmsize]
    mova         m3, [R5+3 * mmsize]
    QPEL_V_LOW [R5+2*mmsize], [R5+1*mmsize], [R5+0*mmsize], [R5+4*mmsize], [r0]
    QPEL_V_LOW [R5+1*mmsize], [R5+0*mmsize], [R5+0*mmsize], [R5+5*mmsize], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [R5+0*mmsize], [R5+0*mmsize], [R5+1*mmsize], [R5+6*mmsize], [r0]
    QPEL_V_LOW [R5+0*mmsize], [R5+1*mmsize], [R5+2*mmsize], [R5+7*mmsize], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [R5+1*mmsize], [R5+2*mmsize], [R5+3*mmsize], [R5+8*mmsize], [r0]
    QPEL_V_LOW [R5+2*mmsize], [R5+3*mmsize], [R5+4*mmsize], [R5+8*mmsize], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [R5+3*mmsize], [R5+4*mmsize], [R5+5*mmsize], [R5+7*mmsize], [r0]
    QPEL_V_LOW [R5+4*mmsize], [R5+5*mmsize], [R5+6*mmsize], [R5+6*mmsize], [r0+r2]

    RET
%endmacro

INIT_XMM sse2
%define PW_ROUND pw_16
%define OP_MOV PUT_OPH
MPEG4_QPEL16_V_LOWPASS put
MPEG4_QPEL8_V_LOWPASS put
%define PW_ROUND pw_16
%define OP_MOV AVG_OPH
MPEG4_QPEL16_V_LOWPASS avg
MPEG4_QPEL8_V_LOWPASS avg
%define PW_ROUND pw_15
%define OP_MOV PUT_OPH
MPEG4_QPEL16_V_LOWPASS put_no_rnd
MPEG4_QPEL8_V_LOWPASS put_no_rnd
