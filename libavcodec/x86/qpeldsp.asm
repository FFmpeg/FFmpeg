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

shuffle_mask16_0: db 2, 1, 1, 0, 0, 0, 1, 0, 1,  2,  2,  3,  4,  3,  5,  4
shuffle_mask16_1: db 5, 6, 6, 7, 8, 7, 9, 8, 9, 10, 10, 11, 12, 11, 13, 12
shuffle_mask16_2: db 0, 1, 1, 2, 3, 2, 3, 3,  3, 2,  2,  1, -1, -1, -1, -1
coeff16_0: times 2 db -1,  3, -1,  3,  3, -1,  3, -1
coeff16_1: times 2 db 20, -6, 20, -6, -6, 20, -6, 20

SECTION .text

%macro PUT_NO_RND_PIXELS_L2 2
; void ff_put_no_rnd_pixels8x9_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
;                                 ptrdiff_t dstStride, ptrdiff_t src1Stride)
cglobal put_no_rnd_pixels%1x%2_l2, 5,6,5
    movu         m0, [r1]
    mova         m1, [r2]
    pcmpeqb      m4, m4
    add          r1, r4
    add          r2, %1
    pxor         m0, m4
    pxor         m1, m4
    pavgb        m0, m1
    pxor         m0, m4
    mova       [r0], m0
    add          r0, r3
    jmp          put_no_rnd_pixels%1x%1_after_prologue_ %+ cpuname

; void ff_put_no_rnd_pixels8x8_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
;                                 ptrdiff_t dstStride, ptrdiff_t src1Stride)
cglobal put_no_rnd_pixels%1x%1_l2, 5,6,5
    pcmpeqb      m4, m4
put_no_rnd_pixels%1x%1_after_prologue_ %+ cpuname:
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
PUT_NO_RND_PIXELS_L2 8, 9
INIT_XMM sse2
PUT_NO_RND_PIXELS_L2 16, 17


; void ff_put_no_rnd_pixels16x16_l2(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
;                                   ptrdiff_t dstStride, ptrdiff_t src1Stride)
INIT_MMX mmxext
cglobal put_no_rnd_pixels16x16_l2, 5,6
    pcmpeqb      m6, m6
    mov         r5d, 16
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


%macro MPEG4_QPEL16_H_LOWPASS 1
cglobal %1_mpeg4_qpel16_h_lowpass, 5, 5, 8, 16*notcpuflag(sse2), dst, src, dstride, srcstride, h
%if notcpuflag(ssse3)
    pxor         m7, m7
%else
    mova         m7, [coeff16_0]
%endif
.loop:
    movu         m0, [srcq]
%if cpuflag(ssse3)
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
    add        srcq, srcstrideq
    paddw        m2, m5
    palignr      m5, m6, m0, 8
    pmaddubsw    m5, [coeff16_1]
    palignr      m6, m0, 12
    pmaddubsw    m6, m7
    paddw        m2, [PW_ROUND]
    paddw        m4, m1
    paddw        m2, m3
    paddw        m4, m5
    psraw        m2, 5
    paddw        m4, m6
    psraw        m4, 5
    packuswb     m2, m4
%ifidn %1, avg
    pavgb        m2, [dstq]
%endif
    mova     [dstq], m2
%else
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
%endif
    add        dstq, dstrideq
    dec          hd
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

INIT_XMM ssse3
%define PW_ROUND pw_16
MPEG4_QPEL16_H_LOWPASS put
%define PW_ROUND pw_16
MPEG4_QPEL16_H_LOWPASS avg
%define PW_ROUND pw_15
MPEG4_QPEL16_H_LOWPASS put_no_rnd

%macro MPEG4_QPEL8_H_LOWPASS 1
cglobal %1_mpeg4_qpel8_h_lowpass, 5, 5, 0
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
    OP_MOV     %5, m5, m4
    SWAP 0,1,2,3
%endmacro

%macro MPEG4_QPEL16_V_LOWPASS 1
cglobal %1_mpeg4_qpel16_v_lowpass, 4, 6, 7, 544
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
cglobal %1_mpeg4_qpel8_v_lowpass, 4, 6, 7, 144
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
