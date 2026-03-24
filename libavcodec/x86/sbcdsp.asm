;******************************************************************************
;* SIMD optimized SBC encoder DSP functions
;*
;* Copyright (C) 2017  Aurelien Jacobs <aurel@gnuage.org>
;* Copyright (C) 2008-2010  Nokia Corporation
;* Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
;* Copyright (C) 2004-2005  Henryk Ploetz <henryk@ploetzli.ch>
;* Copyright (C) 2005-2006  Brad Midgley <bmidgley@xmission.com>
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

scale_mask: times 2 dd 0x8000    ; 1 << (SBC_PROTO_FIXED_SCALE - 1)

SECTION .text

%macro NIDN 3
%ifnidn %2, %3
    %1            %2, %3
%endif
%endmacro

%macro ANALYZE_MAC 6 ; out1, out2, tmp1, tmp2, offset, aligned
    mov%6             %3, [inq+%5]
    mov%6             %4, [inq+%5+mmsize]
%if %5 == 0
    pcmpeqd           m0, m0
    psrld             m0, 31
%endif
    pmaddwd           %3, [constsq+%5]
    pmaddwd           %4, [constsq+%5+mmsize]
%if %5 == 0
    pslld             m0, 15         ; 1 << (SBC_PROTO_FIXED_SCALE - 1) as dword
%endif
    NIDN paddd,       %1, %3
    NIDN paddd,       %2, %4
%endmacro

;*******************************************************************
;void ff_sbc_analyze_4(const int16_t *in, int32_t *out, const int16_t *consts);
;*******************************************************************
INIT_XMM sse2
cglobal sbc_analyze_4, 3, 3, 5, in, out, consts
    ANALYZE_MAC       m1, m2, m1, m2,  0, u
    ANALYZE_MAC       m1, m2, m3, m4, 32, u
    movu              m3, [inq+64]
    paddd             m1, m0
    pmaddwd           m3, [constsq+64]
    paddd             m1, m2
    paddd             m1, m3

    psrad             m1, 16
    packssdw          m1, m1
    pshufd            m2, m1, q0000
    pmaddwd           m2, [constsq+80]
    pshufd            m1, m1, q1111
    pmaddwd           m1, [constsq+96]
    paddd             m1, m2

    mova          [outq], m1

    RET


;*******************************************************************
;void ff_sbc_analyze_8(const int16_t *in, int32_t *out, const int16_t *consts);
;*******************************************************************
INIT_XMM sse2
cglobal sbc_analyze_8, 3, 3, 6, in, out, consts
    ANALYZE_MAC       m1, m2, m1, m2,   0, a
    ANALYZE_MAC       m1, m2, m3, m4,  32, a
    paddd             m1, m0
    ANALYZE_MAC       m1, m2, m3, m4,  64, a
    ANALYZE_MAC       m1, m2, m3, m4,  96, a
    paddd             m2, m0
    ANALYZE_MAC       m1, m2, m3, m4, 128, a

    psrad             m1, 16
    psrad             m2, 16
    packssdw          m1, m2

    pshufd            m2, m1, q0000
    pmaddwd           m0, m2, [constsq+160]
    pshufd            m3, m1, q1111
    pmaddwd           m2, [constsq+176]
    pmaddwd           m4, m3, [constsq+192]
    pshufd            m5, m1, q2222
    pmaddwd           m3, [constsq+208]
    paddd             m0, m4
    pmaddwd           m4, m5, [constsq+224]
    pshufd            m1, m1, q3333
    pmaddwd           m5, [constsq+240]
    paddd             m2, m3
    pmaddwd           m3, m1, [constsq+256]
    paddd             m0, m4
    pmaddwd           m1, [constsq+272]
    paddd             m0, m3
    paddd             m2, m5

    mova          [outq], m0
    paddd             m2, m1
    mova       [outq+16], m2

    RET


;*******************************************************************
;void ff_sbc_calc_scalefactors(const int32_t sb_sample_f[16][2][8],
;                              uint32_t scale_factor[2][8],
;                              int blocks, int channels, int subbands)
;*******************************************************************
INIT_MMX mmx
cglobal sbc_calc_scalefactors, 5, 7, 4, sb_sample_f, scale_factor, blocks, channels, subbands, ptr, blk
    ; subbands = 4 * subbands * channels
    movq          m3, [scale_mask]
    shl           subbandsd, 2
    cmp           channelsd, 2
    jl            .loop_1
    add           subbandsd, 32

.loop_1:
    sub           subbandsq, 8
    lea           ptrq, [sb_sample_fq + subbandsq]

    ; blk = (blocks - 1) * 64;
    lea           blkq, [blocksq - 1]
    shl           blkd, 6

    movq          m0, m3
.loop_2:
    movq          m1, [ptrq+blkq]
    pxor          m2, m2
    pcmpgtd       m1, m2
    paddd         m1, [ptrq+blkq]
    pcmpgtd       m2, m1
    pxor          m1, m2

    por           m0, m1

    sub           blkq, 64
    jns           .loop_2

    movd          blkd, m0
    psrlq         m0,   32
    bsr           blkd, blkd
    sub           blkd, 15    ; SCALE_OUT_BITS
    mov           [scale_factorq + subbandsq], blkd

    movd          blkd, m0
    bsr           blkd, blkd
    sub           blkd, 15    ; SCALE_OUT_BITS
    mov           [scale_factorq + subbandsq + 4], blkd

    cmp           subbandsq, 0
    jg            .loop_1

    emms
    RET
