;*****************************************************************************
;* MMX/SSE2/SSSE3-optimized H.264 QPEL code
;*****************************************************************************
;* Copyright (c) 2004-2005 Michael Niedermayer, Loren Merritt
;* Copyright (C) 2012 Daniel Kang
;*
;* Authors: Daniel Kang <daniel.d.kang@gmail.com>
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

cextern pw_16
cextern pw_5
cextern pb_0

SECTION .text


%macro op_avgh 3
    movh   %3, %2
    pavgb  %1, %3
    movh   %2, %1
%endmacro

%macro op_avg 2-3
    pavgb  %1, %2
    mova   %2, %1
%endmacro

%macro op_puth 2-3
    movh   %2, %1
%endmacro

%macro op_put 2-3
    mova   %2, %1
%endmacro

%macro QPEL4_H_LOWPASS_OP 1
cglobal %1_h264_qpel4_h_lowpass, 4,5 ; dst, src, dstStride, srcStride
    movsxdifnidn  r2, r2d
    movsxdifnidn  r3, r3d
    pxor          m7, m7
    mova          m4, [pw_5]
    mova          m5, [pw_16]
    mov          r4d, 4
.loop:
    movh          m1, [r1-1]
    movh          m2, [r1+0]
    movh          m3, [r1+1]
    movh          m0, [r1+2]
    punpcklbw     m1, m7
    punpcklbw     m2, m7
    punpcklbw     m3, m7
    punpcklbw     m0, m7
    paddw         m1, m0
    paddw         m2, m3
    movh          m0, [r1-2]
    movh          m3, [r1+3]
    punpcklbw     m0, m7
    punpcklbw     m3, m7
    paddw         m0, m3
    psllw         m2, 2
    psubw         m2, m1
    pmullw        m2, m4
    paddw         m0, m5
    paddw         m0, m2
    psraw         m0, 5
    packuswb      m0, m0
    op_%1h        m0, [r0], m6
    add           r0, r2
    add           r1, r3
    dec          r4d
    jg         .loop
    REP_RET
%endmacro

INIT_MMX mmxext
QPEL4_H_LOWPASS_OP put
QPEL4_H_LOWPASS_OP avg

%macro QPEL8_H_LOWPASS_OP 1
cglobal %1_h264_qpel8_h_lowpass, 4,5 ; dst, src, dstStride, srcStride
    movsxdifnidn  r2, r2d
    movsxdifnidn  r3, r3d
    mov          r4d, 8
    pxor          m7, m7
    mova          m6, [pw_5]
.loop:
    mova          m0, [r1]
    mova          m2, [r1+1]
    mova          m1, m0
    mova          m3, m2
    punpcklbw     m0, m7
    punpckhbw     m1, m7
    punpcklbw     m2, m7
    punpckhbw     m3, m7
    paddw         m0, m2
    paddw         m1, m3
    psllw         m0, 2
    psllw         m1, 2
    mova          m2, [r1-1]
    mova          m4, [r1+2]
    mova          m3, m2
    mova          m5, m4
    punpcklbw     m2, m7
    punpckhbw     m3, m7
    punpcklbw     m4, m7
    punpckhbw     m5, m7
    paddw         m2, m4
    paddw         m5, m3
    psubw         m0, m2
    psubw         m1, m5
    pmullw        m0, m6
    pmullw        m1, m6
    movd          m2, [r1-2]
    movd          m5, [r1+7]
    punpcklbw     m2, m7
    punpcklbw     m5, m7
    paddw         m2, m3
    paddw         m4, m5
    mova          m5, [pw_16]
    paddw         m2, m5
    paddw         m4, m5
    paddw         m0, m2
    paddw         m1, m4
    psraw         m0, 5
    psraw         m1, 5
    packuswb      m0, m1
    op_%1         m0, [r0], m4
    add           r0, r2
    add           r1, r3
    dec          r4d
    jg         .loop
    REP_RET
%endmacro

INIT_MMX mmxext
QPEL8_H_LOWPASS_OP put
QPEL8_H_LOWPASS_OP avg

%macro QPEL8_H_LOWPASS_OP_XMM 1
cglobal %1_h264_qpel8_h_lowpass, 4,5,8 ; dst, src, dstStride, srcStride
    movsxdifnidn  r2, r2d
    movsxdifnidn  r3, r3d
    mov          r4d, 8
    pxor          m7, m7
    mova          m6, [pw_5]
.loop:
    movu          m1, [r1-2]
    mova          m0, m1
    punpckhbw     m1, m7
    punpcklbw     m0, m7
    mova          m2, m1
    mova          m3, m1
    mova          m4, m1
    mova          m5, m1
    palignr       m4, m0, 2
    palignr       m3, m0, 4
    palignr       m2, m0, 6
    palignr       m1, m0, 8
    palignr       m5, m0, 10
    paddw         m0, m5
    paddw         m2, m3
    paddw         m1, m4
    psllw         m2, 2
    psubw         m2, m1
    paddw         m0, [pw_16]
    pmullw        m2, m6
    paddw         m2, m0
    psraw         m2, 5
    packuswb      m2, m2
    op_%1h        m2, [r0], m4
    add           r1, r3
    add           r0, r2
    dec          r4d
    jne        .loop
    REP_RET
%endmacro

INIT_XMM ssse3
QPEL8_H_LOWPASS_OP_XMM put
QPEL8_H_LOWPASS_OP_XMM avg


%macro QPEL4_H_LOWPASS_L2_OP 1
cglobal %1_h264_qpel4_h_lowpass_l2, 5,6 ; dst, src, src2, dstStride, srcStride
    movsxdifnidn  r3, r3d
    movsxdifnidn  r4, r4d
    pxor          m7, m7
    mova          m4, [pw_5]
    mova          m5, [pw_16]
    mov          r5d, 4
.loop:
    movh          m1, [r1-1]
    movh          m2, [r1+0]
    movh          m3, [r1+1]
    movh          m0, [r1+2]
    punpcklbw     m1, m7
    punpcklbw     m2, m7
    punpcklbw     m3, m7
    punpcklbw     m0, m7
    paddw         m1, m0
    paddw         m2, m3
    movh          m0, [r1-2]
    movh          m3, [r1+3]
    punpcklbw     m0, m7
    punpcklbw     m3, m7
    paddw         m0, m3
    psllw         m2, 2
    psubw         m2, m1
    pmullw        m2, m4
    paddw         m0, m5
    paddw         m0, m2
    movh          m3, [r2]
    psraw         m0, 5
    packuswb      m0, m0
    pavgb         m0, m3
    op_%1h        m0, [r0], m6
    add           r0, r3
    add           r1, r3
    add           r2, r4
    dec          r5d
    jg         .loop
    REP_RET
%endmacro

INIT_MMX mmxext
QPEL4_H_LOWPASS_L2_OP put
QPEL4_H_LOWPASS_L2_OP avg


%macro QPEL8_H_LOWPASS_L2_OP 1
cglobal %1_h264_qpel8_h_lowpass_l2, 5,6 ; dst, src, src2, dstStride, srcStride
    movsxdifnidn  r3, r3d
    movsxdifnidn  r4, r4d
    mov          r5d, 8
    pxor          m7, m7
    mova          m6, [pw_5]
.loop:
    mova          m0, [r1]
    mova          m2, [r1+1]
    mova          m1, m0
    mova          m3, m2
    punpcklbw     m0, m7
    punpckhbw     m1, m7
    punpcklbw     m2, m7
    punpckhbw     m3, m7
    paddw         m0, m2
    paddw         m1, m3
    psllw         m0, 2
    psllw         m1, 2
    mova          m2, [r1-1]
    mova          m4, [r1+2]
    mova          m3, m2
    mova          m5, m4
    punpcklbw     m2, m7
    punpckhbw     m3, m7
    punpcklbw     m4, m7
    punpckhbw     m5, m7
    paddw         m2, m4
    paddw         m5, m3
    psubw         m0, m2
    psubw         m1, m5
    pmullw        m0, m6
    pmullw        m1, m6
    movd          m2, [r1-2]
    movd          m5, [r1+7]
    punpcklbw     m2, m7
    punpcklbw     m5, m7
    paddw         m2, m3
    paddw         m4, m5
    mova          m5, [pw_16]
    paddw         m2, m5
    paddw         m4, m5
    paddw         m0, m2
    paddw         m1, m4
    psraw         m0, 5
    psraw         m1, 5
    mova          m4, [r2]
    packuswb      m0, m1
    pavgb         m0, m4
    op_%1         m0, [r0], m4
    add           r0, r3
    add           r1, r3
    add           r2, r4
    dec          r5d
    jg         .loop
    REP_RET
%endmacro

INIT_MMX mmxext
QPEL8_H_LOWPASS_L2_OP put
QPEL8_H_LOWPASS_L2_OP avg


%macro QPEL8_H_LOWPASS_L2_OP_XMM 1
cglobal %1_h264_qpel8_h_lowpass_l2, 5,6,8 ; dst, src, src2, dstStride, src2Stride
    movsxdifnidn  r3, r3d
    movsxdifnidn  r4, r4d
    mov          r5d, 8
    pxor          m7, m7
    mova          m6, [pw_5]
.loop:
    lddqu         m1, [r1-2]
    mova          m0, m1
    punpckhbw     m1, m7
    punpcklbw     m0, m7
    mova          m2, m1
    mova          m3, m1
    mova          m4, m1
    mova          m5, m1
    palignr       m4, m0, 2
    palignr       m3, m0, 4
    palignr       m2, m0, 6
    palignr       m1, m0, 8
    palignr       m5, m0, 10
    paddw         m0, m5
    paddw         m2, m3
    paddw         m1, m4
    psllw         m2, 2
    movh          m3, [r2]
    psubw         m2, m1
    paddw         m0, [pw_16]
    pmullw        m2, m6
    paddw         m2, m0
    psraw         m2, 5
    packuswb      m2, m2
    pavgb         m2, m3
    op_%1h        m2, [r0], m4
    add           r1, r3
    add           r0, r3
    add           r2, r4
    dec          r5d
    jg         .loop
    REP_RET
%endmacro

INIT_XMM ssse3
QPEL8_H_LOWPASS_L2_OP_XMM put
QPEL8_H_LOWPASS_L2_OP_XMM avg


; All functions that call this are required to have function arguments of
; dst, src, dstStride, srcStride
%macro FILT_V 1
    mova      m6, m2
    movh      m5, [r1]
    paddw     m6, m3
    psllw     m6, 2
    psubw     m6, m1
    psubw     m6, m4
    punpcklbw m5, m7
    pmullw    m6, [pw_5]
    paddw     m0, [pw_16]
    add       r1, r3
    paddw     m0, m5
    paddw     m6, m0
    psraw     m6, 5
    packuswb  m6, m6
    op_%1h    m6, [r0], m0 ; 1
    add       r0, r2
    SWAP       0, 1, 2, 3, 4, 5
%endmacro

%macro QPEL4_V_LOWPASS_OP 1
cglobal %1_h264_qpel4_v_lowpass, 4,4 ; dst, src, dstStride, srcStride
    movsxdifnidn  r2, r2d
    movsxdifnidn  r3, r3d
    sub           r1, r3
    sub           r1, r3
    pxor          m7, m7
    movh          m0, [r1]
    movh          m1, [r1+r3]
    lea           r1, [r1+2*r3]
    movh          m2, [r1]
    movh          m3, [r1+r3]
    lea           r1, [r1+2*r3]
    movh          m4, [r1]
    add           r1, r3
    punpcklbw     m0, m7
    punpcklbw     m1, m7
    punpcklbw     m2, m7
    punpcklbw     m3, m7
    punpcklbw     m4, m7
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    RET
%endmacro

INIT_MMX mmxext
QPEL4_V_LOWPASS_OP put
QPEL4_V_LOWPASS_OP avg



%macro QPEL8OR16_V_LOWPASS_OP 1
%if cpuflag(sse2)
cglobal %1_h264_qpel8or16_v_lowpass, 5,5,8 ; dst, src, dstStride, srcStride, h
    movsxdifnidn  r2, r2d
    movsxdifnidn  r3, r3d
    sub           r1, r3
    sub           r1, r3
%else
cglobal %1_h264_qpel8or16_v_lowpass_op, 5,5,8 ; dst, src, dstStride, srcStride, h
    movsxdifnidn  r2, r2d
    movsxdifnidn  r3, r3d
%endif
    pxor          m7, m7
    movh          m0, [r1]
    movh          m1, [r1+r3]
    lea           r1, [r1+2*r3]
    movh          m2, [r1]
    movh          m3, [r1+r3]
    lea           r1, [r1+2*r3]
    movh          m4, [r1]
    add           r1, r3
    punpcklbw     m0, m7
    punpcklbw     m1, m7
    punpcklbw     m2, m7
    punpcklbw     m3, m7
    punpcklbw     m4, m7
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    cmp          r4d, 16
    jne         .end
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
    FILT_V        %1
.end:
    REP_RET
%endmacro

INIT_MMX mmxext
QPEL8OR16_V_LOWPASS_OP put
QPEL8OR16_V_LOWPASS_OP avg

INIT_XMM sse2
QPEL8OR16_V_LOWPASS_OP put
QPEL8OR16_V_LOWPASS_OP avg


; All functions that use this are required to have args:
; src, tmp, srcSize
%macro FILT_HV 1 ; offset
    mova           m6, m2
    movh           m5, [r0]
    paddw          m6, m3
    psllw          m6, 2
    paddw          m0, [pw_16]
    psubw          m6, m1
    psubw          m6, m4
    punpcklbw      m5, m7
    pmullw         m6, [pw_5]
    paddw          m0, m5
    add            r0, r2
    paddw          m6, m0
    mova      [r1+%1], m6
    SWAP            0, 1, 2, 3, 4, 5
%endmacro

%macro QPEL4_HV1_LOWPASS_OP 1
cglobal %1_h264_qpel4_hv_lowpass_v, 3,3 ; src, tmp, srcStride
    movsxdifnidn  r2, r2d
    pxor          m7, m7
    movh          m0, [r0]
    movh          m1, [r0+r2]
    lea           r0, [r0+2*r2]
    movh          m2, [r0]
    movh          m3, [r0+r2]
    lea           r0, [r0+2*r2]
    movh          m4, [r0]
    add           r0, r2
    punpcklbw     m0, m7
    punpcklbw     m1, m7
    punpcklbw     m2, m7
    punpcklbw     m3, m7
    punpcklbw     m4, m7
    FILT_HV       0*24
    FILT_HV       1*24
    FILT_HV       2*24
    FILT_HV       3*24
    RET

cglobal %1_h264_qpel4_hv_lowpass_h, 3,4 ; tmp, dst, dstStride
    movsxdifnidn  r2, r2d
    mov          r3d, 4
.loop:
    mova          m0, [r0]
    paddw         m0, [r0+10]
    mova          m1, [r0+2]
    paddw         m1, [r0+8]
    mova          m2, [r0+4]
    paddw         m2, [r0+6]
    psubw         m0, m1
    psraw         m0, 2
    psubw         m0, m1
    paddsw        m0, m2
    psraw         m0, 2
    paddw         m0, m2
    psraw         m0, 6
    packuswb      m0, m0
    op_%1h        m0, [r1], m7
    add           r0, 24
    add           r1, r2
    dec          r3d
    jnz        .loop
    REP_RET
%endmacro

INIT_MMX mmxext
QPEL4_HV1_LOWPASS_OP put
QPEL4_HV1_LOWPASS_OP avg

%macro QPEL8OR16_HV1_LOWPASS_OP 1
cglobal %1_h264_qpel8or16_hv1_lowpass_op, 4,4,8 ; src, tmp, srcStride, size
    movsxdifnidn  r2, r2d
    pxor          m7, m7
    movh          m0, [r0]
    movh          m1, [r0+r2]
    lea           r0, [r0+2*r2]
    movh          m2, [r0]
    movh          m3, [r0+r2]
    lea           r0, [r0+2*r2]
    movh          m4, [r0]
    add           r0, r2
    punpcklbw     m0, m7
    punpcklbw     m1, m7
    punpcklbw     m2, m7
    punpcklbw     m3, m7
    punpcklbw     m4, m7
    FILT_HV     0*48
    FILT_HV     1*48
    FILT_HV     2*48
    FILT_HV     3*48
    FILT_HV     4*48
    FILT_HV     5*48
    FILT_HV     6*48
    FILT_HV     7*48
    cmp          r3d, 16
    jne         .end
    FILT_HV     8*48
    FILT_HV     9*48
    FILT_HV    10*48
    FILT_HV    11*48
    FILT_HV    12*48
    FILT_HV    13*48
    FILT_HV    14*48
    FILT_HV    15*48
.end:
    REP_RET
%endmacro

INIT_MMX mmxext
QPEL8OR16_HV1_LOWPASS_OP put
QPEL8OR16_HV1_LOWPASS_OP avg

INIT_XMM sse2
QPEL8OR16_HV1_LOWPASS_OP put



%macro QPEL8OR16_HV2_LOWPASS_OP 1
; unused is to match ssse3 and mmxext args
cglobal %1_h264_qpel8or16_hv2_lowpass_op, 5,5 ; dst, tmp, dstStride, unused, h
    movsxdifnidn  r2, r2d
.loop:
    mova          m0, [r1]
    mova          m3, [r1+8]
    mova          m1, [r1+2]
    mova          m4, [r1+10]
    paddw         m0, m4
    paddw         m1, m3
    paddw         m3, [r1+18]
    paddw         m4, [r1+16]
    mova          m2, [r1+4]
    mova          m5, [r1+12]
    paddw         m2, [r1+6]
    paddw         m5, [r1+14]
    psubw         m0, m1
    psubw         m3, m4
    psraw         m0, 2
    psraw         m3, 2
    psubw         m0, m1
    psubw         m3, m4
    paddsw        m0, m2
    paddsw        m3, m5
    psraw         m0, 2
    psraw         m3, 2
    paddw         m0, m2
    paddw         m3, m5
    psraw         m0, 6
    psraw         m3, 6
    packuswb      m0, m3
    op_%1         m0, [r0], m7
    add           r1, 48
    add           r0, r2
    dec          r4d
    jne        .loop
    REP_RET
%endmacro

INIT_MMX mmxext
QPEL8OR16_HV2_LOWPASS_OP put
QPEL8OR16_HV2_LOWPASS_OP avg

%macro QPEL8OR16_HV2_LOWPASS_OP_XMM 1
cglobal %1_h264_qpel8or16_hv2_lowpass, 5,5,8 ; dst, tmp, dstStride, tmpStride, size
    movsxdifnidn  r2, r2d
    movsxdifnidn  r3, r3d
    cmp          r4d, 16
    je         .op16
.loop8:
    mova          m1, [r1+16]
    mova          m0, [r1]
    mova          m2, m1
    mova          m3, m1
    mova          m4, m1
    mova          m5, m1
    palignr       m5, m0, 10
    palignr       m4, m0, 8
    palignr       m3, m0, 6
    palignr       m2, m0, 4
    palignr       m1, m0, 2
    paddw         m0, m5
    paddw         m1, m4
    paddw         m2, m3
    psubw         m0, m1
    psraw         m0, 2
    psubw         m0, m1
    paddw         m0, m2
    psraw         m0, 2
    paddw         m0, m2
    psraw         m0, 6
    packuswb      m0, m0
    op_%1h        m0, [r0], m7
    add           r1, 48
    add           r0, r2
    dec          r4d
    jne       .loop8
    jmp        .done
.op16:
    mova          m4, [r1+32]
    mova          m5, [r1+16]
    mova          m7, [r1]
    mova          m3, m4
    mova          m2, m4
    mova          m1, m4
    mova          m0, m4
    palignr       m0, m5, 10
    palignr       m1, m5, 8
    palignr       m2, m5, 6
    palignr       m3, m5, 4
    palignr       m4, m5, 2
    paddw         m0, m5
    paddw         m1, m4
    paddw         m2, m3
    mova          m6, m5
    mova          m4, m5
    mova          m3, m5
    palignr       m4, m7, 8
    palignr       m6, m7, 2
    palignr       m3, m7, 10
    paddw         m4, m6
    mova          m6, m5
    palignr       m5, m7, 6
    palignr       m6, m7, 4
    paddw         m3, m7
    paddw         m5, m6
    psubw         m0, m1
    psubw         m3, m4
    psraw         m0, 2
    psraw         m3, 2
    psubw         m0, m1
    psubw         m3, m4
    paddw         m0, m2
    paddw         m3, m5
    psraw         m0, 2
    psraw         m3, 2
    paddw         m0, m2
    paddw         m3, m5
    psraw         m0, 6
    psraw         m3, 6
    packuswb      m3, m0
    op_%1         m3, [r0], m7
    add           r1, 48
    add           r0, r2
    dec          r4d
    jne        .op16
.done:
    REP_RET
%endmacro

INIT_XMM ssse3
QPEL8OR16_HV2_LOWPASS_OP_XMM put
QPEL8OR16_HV2_LOWPASS_OP_XMM avg


%macro PIXELS4_L2_SHIFT5 1
cglobal %1_pixels4_l2_shift5,6,6 ; dst, src16, src8, dstStride, src8Stride, h
    movsxdifnidn  r3, r3d
    movsxdifnidn  r4, r4d
    mova          m0, [r1]
    mova          m1, [r1+24]
    psraw         m0, 5
    psraw         m1, 5
    packuswb      m0, m0
    packuswb      m1, m1
    pavgb         m0, [r2]
    pavgb         m1, [r2+r4]
    op_%1h        m0, [r0], m4
    op_%1h        m1, [r0+r3], m5
    lea           r2, [r2+r4*2]
    lea           r0, [r0+r3*2]
    mova          m0, [r1+48]
    mova          m1, [r1+72]
    psraw         m0, 5
    psraw         m1, 5
    packuswb      m0, m0
    packuswb      m1, m1
    pavgb         m0, [r2]
    pavgb         m1, [r2+r4]
    op_%1h        m0, [r0], m4
    op_%1h        m1, [r0+r3], m5
    RET
%endmacro

INIT_MMX mmxext
PIXELS4_L2_SHIFT5 put
PIXELS4_L2_SHIFT5 avg


%macro PIXELS8_L2_SHIFT5 1
cglobal %1_pixels8_l2_shift5, 6, 6 ; dst, src16, src8, dstStride, src8Stride, h
    movsxdifnidn  r3, r3d
    movsxdifnidn  r4, r4d
.loop:
    mova          m0, [r1]
    mova          m1, [r1+8]
    mova          m2, [r1+48]
    mova          m3, [r1+48+8]
    psraw         m0, 5
    psraw         m1, 5
    psraw         m2, 5
    psraw         m3, 5
    packuswb      m0, m1
    packuswb      m2, m3
    pavgb         m0, [r2]
    pavgb         m2, [r2+r4]
    op_%1         m0, [r0], m4
    op_%1         m2, [r0+r3], m5
    lea           r2, [r2+2*r4]
    add           r1, 48*2
    lea           r0, [r0+2*r3]
    sub          r5d, 2
    jne        .loop
    REP_RET
%endmacro

INIT_MMX mmxext
PIXELS8_L2_SHIFT5 put
PIXELS8_L2_SHIFT5 avg


%if ARCH_X86_64
%macro QPEL16_H_LOWPASS_L2_OP 1
cglobal %1_h264_qpel16_h_lowpass_l2, 5, 6, 16 ; dst, src, src2, dstStride, src2Stride
    movsxdifnidn  r3, r3d
    movsxdifnidn  r4, r4d
    mov          r5d, 16
    pxor         m15, m15
    mova         m14, [pw_5]
    mova         m13, [pw_16]
.loop:
    lddqu         m1, [r1+6]
    lddqu         m7, [r1-2]
    mova          m0, m1
    punpckhbw     m1, m15
    punpcklbw     m0, m15
    punpcklbw     m7, m15
    mova          m2, m1
    mova          m6, m0
    mova          m3, m1
    mova          m8, m0
    mova          m4, m1
    mova          m9, m0
    mova         m12, m0
    mova         m11, m1
    palignr      m11, m0, 10
    palignr      m12, m7, 10
    palignr       m4, m0, 2
    palignr       m9, m7, 2
    palignr       m3, m0, 4
    palignr       m8, m7, 4
    palignr       m2, m0, 6
    palignr       m6, m7, 6
    paddw        m11, m0
    palignr       m1, m0, 8
    palignr       m0, m7, 8
    paddw         m7, m12
    paddw         m2, m3
    paddw         m6, m8
    paddw         m1, m4
    paddw         m0, m9
    psllw         m2, 2
    psllw         m6, 2
    psubw         m2, m1
    psubw         m6, m0
    paddw        m11, m13
    paddw         m7, m13
    pmullw        m2, m14
    pmullw        m6, m14
    lddqu         m3, [r2]
    paddw         m2, m11
    paddw         m6, m7
    psraw         m2, 5
    psraw         m6, 5
    packuswb      m6, m2
    pavgb         m6, m3
    op_%1         m6, [r0], m11
    add           r1, r3
    add           r0, r3
    add           r2, r4
    dec          r5d
    jg         .loop
    REP_RET
%endmacro

INIT_XMM ssse3
QPEL16_H_LOWPASS_L2_OP put
QPEL16_H_LOWPASS_L2_OP avg
%endif
