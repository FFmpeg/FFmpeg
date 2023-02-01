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
cextern pb_1
cextern pw_3
cextern pw_15
cextern pw_16
cextern pw_20


SECTION .text

; void ff_put_no_rnd_pixels8_l2(uint8_t *dst, uint8_t *src1, uint8_t *src2, int dstStride, int src1Stride, int h)
%macro PUT_NO_RND_PIXELS8_L2 0
cglobal put_no_rnd_pixels8_l2, 6,6
    movsxdifnidn r4, r4d
    movsxdifnidn r3, r3d
    pcmpeqb      m6, m6
    test        r5d, 1
    je .loop
    mova         m0, [r1]
    mova         m1, [r2]
    add          r1, r4
    add          r2, 8
    pxor         m0, m6
    pxor         m1, m6
    PAVGB        m0, m1
    pxor         m0, m6
    mova       [r0], m0
    add          r0, r3
    dec r5d
.loop:
    mova         m0, [r1]
    add          r1, r4
    mova         m1, [r1]
    add          r1, r4
    mova         m2, [r2]
    mova         m3, [r2+8]
    pxor         m0, m6
    pxor         m1, m6
    pxor         m2, m6
    pxor         m3, m6
    PAVGB        m0, m2
    PAVGB        m1, m3
    pxor         m0, m6
    pxor         m1, m6
    mova       [r0], m0
    add          r0, r3
    mova       [r0], m1
    add          r0, r3
    mova         m0, [r1]
    add          r1, r4
    mova         m1, [r1]
    add          r1, r4
    mova         m2, [r2+16]
    mova         m3, [r2+24]
    pxor         m0, m6
    pxor         m1, m6
    pxor         m2, m6
    pxor         m3, m6
    PAVGB        m0, m2
    PAVGB        m1, m3
    pxor         m0, m6
    pxor         m1, m6
    mova       [r0], m0
    add          r0, r3
    mova       [r0], m1
    add          r0, r3
    add          r2, 32
    sub         r5d, 4
    jne .loop
    RET
%endmacro

INIT_MMX mmxext
PUT_NO_RND_PIXELS8_L2


; void ff_put_no_rnd_pixels16_l2(uint8_t *dst, uint8_t *src1, uint8_t *src2, int dstStride, int src1Stride, int h)
%macro PUT_NO_RND_PIXELS16_l2 0
cglobal put_no_rnd_pixels16_l2, 6,6
    movsxdifnidn r3, r3d
    movsxdifnidn r4, r4d
    pcmpeqb      m6, m6
    test        r5d, 1
    je .loop
    mova         m0, [r1]
    mova         m1, [r1+8]
    mova         m2, [r2]
    mova         m3, [r2+8]
    pxor         m0, m6
    pxor         m1, m6
    pxor         m2, m6
    pxor         m3, m6
    PAVGB        m0, m2
    PAVGB        m1, m3
    pxor         m0, m6
    pxor         m1, m6
    add          r1, r4
    add          r2, 16
    mova       [r0], m0
    mova     [r0+8], m1
    add          r0, r3
    dec r5d
.loop:
    mova         m0, [r1]
    mova         m1, [r1+8]
    add          r1, r4
    mova         m2, [r2]
    mova         m3, [r2+8]
    pxor         m0, m6
    pxor         m1, m6
    pxor         m2, m6
    pxor         m3, m6
    PAVGB        m0, m2
    PAVGB        m1, m3
    pxor         m0, m6
    pxor         m1, m6
    mova       [r0], m0
    mova     [r0+8], m1
    add          r0, r3
    mova         m0, [r1]
    mova         m1, [r1+8]
    add          r1, r4
    mova         m2, [r2+16]
    mova         m3, [r2+24]
    pxor         m0, m6
    pxor         m1, m6
    pxor         m2, m6
    pxor         m3, m6
    PAVGB        m0, m2
    PAVGB        m1, m3
    pxor         m0, m6
    pxor         m1, m6
    mova       [r0], m0
    mova     [r0+8], m1
    add          r0, r3
    add          r2, 32
    sub         r5d, 2
    jne .loop
    RET
%endmacro

INIT_MMX mmxext
PUT_NO_RND_PIXELS16_l2

%macro MPEG4_QPEL16_H_LOWPASS 1
cglobal %1_mpeg4_qpel16_h_lowpass, 5, 5, 0, 16
    movsxdifnidn r2, r2d
    movsxdifnidn r3, r3d
    pxor         m7, m7
.loop:
    mova         m0, [r1]
    mova         m1, m0
    mova         m2, m0
    punpcklbw    m0, m7
    punpckhbw    m1, m7
    pshufw       m5, m0, 0x90
    pshufw       m6, m0, 0x41
    mova         m3, m2
    mova         m4, m2
    psllq        m2, 8
    psllq        m3, 16
    psllq        m4, 24
    punpckhbw    m2, m7
    punpckhbw    m3, m7
    punpckhbw    m4, m7
    paddw        m5, m3
    paddw        m6, m2
    paddw        m5, m5
    psubw        m6, m5
    pshufw       m5, m0, 6
    pmullw       m6, [pw_3]
    paddw        m0, m4
    paddw        m5, m1
    pmullw       m0, [pw_20]
    psubw        m0, m5
    paddw        m6, [PW_ROUND]
    paddw        m0, m6
    psraw        m0, 5
    mova    [rsp+8], m0
    mova         m0, [r1+5]
    mova         m5, m0
    mova         m6, m0
    psrlq        m0, 8
    psrlq        m5, 16
    punpcklbw    m0, m7
    punpcklbw    m5, m7
    paddw        m2, m0
    paddw        m3, m5
    paddw        m2, m2
    psubw        m3, m2
    mova         m2, m6
    psrlq        m6, 24
    punpcklbw    m2, m7
    punpcklbw    m6, m7
    pmullw       m3, [pw_3]
    paddw        m1, m2
    paddw        m4, m6
    pmullw       m1, [pw_20]
    psubw        m3, m4
    paddw        m1, [PW_ROUND]
    paddw        m3, m1
    psraw        m3, 5
    mova         m1, [rsp+8]
    packuswb     m1, m3
    OP_MOV     [r0], m1, m4
    mova         m1, [r1+9]
    mova         m4, m1
    mova         m3, m1
    psrlq        m1, 8
    psrlq        m4, 16
    punpcklbw    m1, m7
    punpcklbw    m4, m7
    paddw        m5, m1
    paddw        m0, m4
    paddw        m5, m5
    psubw        m0, m5
    mova         m5, m3
    psrlq        m3, 24
    pmullw       m0, [pw_3]
    punpcklbw    m3, m7
    paddw        m2, m3
    psubw        m0, m2
    mova         m2, m5
    punpcklbw    m2, m7
    punpckhbw    m5, m7
    paddw        m6, m2
    pmullw       m6, [pw_20]
    paddw        m0, [PW_ROUND]
    paddw        m0, m6
    psraw        m0, 5
    paddw        m3, m5
    pshufw       m6, m5, 0xf9
    paddw        m6, m4
    pshufw       m4, m5, 0xbe
    pshufw       m5, m5, 0x6f
    paddw        m4, m1
    paddw        m5, m2
    paddw        m6, m6
    psubw        m4, m6
    pmullw       m3, [pw_20]
    pmullw       m4, [pw_3]
    psubw        m3, m5
    paddw        m4, [PW_ROUND]
    paddw        m4, m3
    psraw        m4, 5
    packuswb     m0, m4
    OP_MOV   [r0+8], m0, m4
    add          r1, r3
    add          r0, r2
    dec r4d
    jne .loop
    RET
%endmacro

%macro PUT_OP 2-3
    mova %1, %2
%endmacro

%macro AVG_OP 2-3
    mova  %3, %1
    pavgb %2, %3
    mova  %1, %2
%endmacro

INIT_MMX mmxext
%define PW_ROUND pw_16
%define OP_MOV PUT_OP
MPEG4_QPEL16_H_LOWPASS put
%define PW_ROUND pw_16
%define OP_MOV AVG_OP
MPEG4_QPEL16_H_LOWPASS avg
%define PW_ROUND pw_15
%define OP_MOV PUT_OP
MPEG4_QPEL16_H_LOWPASS put_no_rnd



%macro MPEG4_QPEL8_H_LOWPASS 1
cglobal %1_mpeg4_qpel8_h_lowpass, 5, 5, 0, 8
    movsxdifnidn r2, r2d
    movsxdifnidn r3, r3d
    pxor         m7, m7
.loop:
    mova         m0, [r1]
    mova         m1, m0
    mova         m2, m0
    punpcklbw    m0, m7
    punpckhbw    m1, m7
    pshufw       m5, m0, 0x90
    pshufw       m6, m0, 0x41
    mova         m3, m2
    mova         m4, m2
    psllq        m2, 8
    psllq        m3, 16
    psllq        m4, 24
    punpckhbw    m2, m7
    punpckhbw    m3, m7
    punpckhbw    m4, m7
    paddw        m5, m3
    paddw        m6, m2
    paddw        m5, m5
    psubw        m6, m5
    pshufw       m5, m0, 0x6
    pmullw       m6, [pw_3]
    paddw        m0, m4
    paddw        m5, m1
    pmullw       m0, [pw_20]
    psubw        m0, m5
    paddw        m6, [PW_ROUND]
    paddw        m0, m6
    psraw        m0, 5
    movh         m5, [r1+5]
    punpcklbw    m5, m7
    pshufw       m6, m5, 0xf9
    paddw        m1, m5
    paddw        m2, m6
    pshufw       m6, m5, 0xbe
    pshufw       m5, m5, 0x6f
    paddw        m3, m6
    paddw        m4, m5
    paddw        m2, m2
    psubw        m3, m2
    pmullw       m1, [pw_20]
    pmullw       m3, [pw_3]
    psubw        m3, m4
    paddw        m1, [PW_ROUND]
    paddw        m3, m1
    psraw        m3, 5
    packuswb     m0, m3
    OP_MOV     [r0], m0, m4
    add          r1, r3
    add          r0, r2
    dec r4d
    jne .loop
    RET
%endmacro

INIT_MMX mmxext
%define PW_ROUND pw_16
%define OP_MOV PUT_OP
MPEG4_QPEL8_H_LOWPASS put
%define PW_ROUND pw_16
%define OP_MOV AVG_OP
MPEG4_QPEL8_H_LOWPASS avg
%define PW_ROUND pw_15
%define OP_MOV PUT_OP
MPEG4_QPEL8_H_LOWPASS put_no_rnd



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
    OP_MOV     %5, m5, m7
    SWAP 0,1,2,3
%endmacro

%macro MPEG4_QPEL16_V_LOWPASS 1
cglobal %1_mpeg4_qpel16_v_lowpass, 4, 6, 0, 544
    movsxdifnidn r2, r2d
    movsxdifnidn r3, r3d

    mov         r4d, 17
    mov          r5, rsp
    pxor         m7, m7
.looph:
    mova         m0, [r1]
    mova         m1, [r1]
    mova         m2, [r1+8]
    mova         m3, [r1+8]
    punpcklbw    m0, m7
    punpckhbw    m1, m7
    punpcklbw    m2, m7
    punpckhbw    m3, m7
    mova       [r5], m0
    mova  [r5+0x88], m1
    mova [r5+0x110], m2
    mova [r5+0x198], m3
    add          r5, 8
    add          r1, r3
    dec r4d
    jne .looph


    ; NOTE: r1 CHANGES VALUES: r1 -> 4 - 14*dstStride
    mov         r4d, 4
    mov          r1, 4
    neg          r2
    lea          r1, [r1+r2*8]
    lea          r1, [r1+r2*4]
    lea          r1, [r1+r2*2]
    neg          r2
    mov          r5, rsp
.loopv:
    pxor         m7, m7
    mova         m0, [r5+ 0x0]
    mova         m1, [r5+ 0x8]
    mova         m2, [r5+0x10]
    mova         m3, [r5+0x18]
    QPEL_V_LOW [r5+0x10], [r5+ 0x8], [r5+ 0x0], [r5+0x20], [r0]
    QPEL_V_LOW [r5+ 0x8], [r5+ 0x0], [r5+ 0x0], [r5+0x28], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+ 0x0], [r5+ 0x0], [r5+ 0x8], [r5+0x30], [r0]
    QPEL_V_LOW [r5+ 0x0], [r5+ 0x8], [r5+0x10], [r5+0x38], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+ 0x8], [r5+0x10], [r5+0x18], [r5+0x40], [r0]
    QPEL_V_LOW [r5+0x10], [r5+0x18], [r5+0x20], [r5+0x48], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+0x18], [r5+0x20], [r5+0x28], [r5+0x50], [r0]
    QPEL_V_LOW [r5+0x20], [r5+0x28], [r5+0x30], [r5+0x58], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+0x28], [r5+0x30], [r5+0x38], [r5+0x60], [r0]
    QPEL_V_LOW [r5+0x30], [r5+0x38], [r5+0x40], [r5+0x68], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+0x38], [r5+0x40], [r5+0x48], [r5+0x70], [r0]
    QPEL_V_LOW [r5+0x40], [r5+0x48], [r5+0x50], [r5+0x78], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+0x48], [r5+0x50], [r5+0x58], [r5+0x80], [r0]
    QPEL_V_LOW [r5+0x50], [r5+0x58], [r5+0x60], [r5+0x80], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+0x58], [r5+0x60], [r5+0x68], [r5+0x78], [r0]
    QPEL_V_LOW [r5+0x60], [r5+0x68], [r5+0x70], [r5+0x70], [r0+r2]

    add    r5, 0x88
    add    r0, r1
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

INIT_MMX mmxext
%define PW_ROUND pw_16
%define OP_MOV PUT_OPH
MPEG4_QPEL16_V_LOWPASS put
%define PW_ROUND pw_16
%define OP_MOV AVG_OPH
MPEG4_QPEL16_V_LOWPASS avg
%define PW_ROUND pw_15
%define OP_MOV PUT_OPH
MPEG4_QPEL16_V_LOWPASS put_no_rnd



%macro MPEG4_QPEL8_V_LOWPASS 1
cglobal %1_mpeg4_qpel8_v_lowpass, 4, 6, 0, 288
    movsxdifnidn r2, r2d
    movsxdifnidn r3, r3d

    mov         r4d, 9
    mov          r5, rsp
    pxor         m7, m7
.looph:
    mova         m0, [r1]
    mova         m1, [r1]
    punpcklbw    m0, m7
    punpckhbw    m1, m7
    mova       [r5], m0
    mova  [r5+0x48], m1
    add          r5, 8
    add          r1, r3
    dec r4d
    jne .looph


    ; NOTE: r1 CHANGES VALUES: r1 -> 4 - 6*dstStride
    mov         r4d, 2
    mov          r1, 4
    neg          r2
    lea          r1, [r1+r2*4]
    lea          r1, [r1+r2*2]
    neg          r2
    mov          r5, rsp
.loopv:
    pxor         m7, m7
    mova         m0, [r5+ 0x0]
    mova         m1, [r5+ 0x8]
    mova         m2, [r5+0x10]
    mova         m3, [r5+0x18]
    QPEL_V_LOW [r5+0x10], [r5+ 0x8], [r5+ 0x0], [r5+0x20], [r0]
    QPEL_V_LOW [r5+ 0x8], [r5+ 0x0], [r5+ 0x0], [r5+0x28], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+ 0x0], [r5+ 0x0], [r5+ 0x8], [r5+0x30], [r0]
    QPEL_V_LOW [r5+ 0x0], [r5+ 0x8], [r5+0x10], [r5+0x38], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+ 0x8], [r5+0x10], [r5+0x18], [r5+0x40], [r0]
    QPEL_V_LOW [r5+0x10], [r5+0x18], [r5+0x20], [r5+0x40], [r0+r2]
    lea    r0, [r0+r2*2]
    QPEL_V_LOW [r5+0x18], [r5+0x20], [r5+0x28], [r5+0x38], [r0]
    QPEL_V_LOW [r5+0x20], [r5+0x28], [r5+0x30], [r5+0x30], [r0+r2]

    add    r5, 0x48
    add    r0, r1
    dec r4d
    jne .loopv
    RET
%endmacro

INIT_MMX mmxext
%define PW_ROUND pw_16
%define OP_MOV PUT_OPH
MPEG4_QPEL8_V_LOWPASS put
%define PW_ROUND pw_16
%define OP_MOV AVG_OPH
MPEG4_QPEL8_V_LOWPASS avg
%define PW_ROUND pw_15
%define OP_MOV PUT_OPH
MPEG4_QPEL8_V_LOWPASS put_no_rnd
