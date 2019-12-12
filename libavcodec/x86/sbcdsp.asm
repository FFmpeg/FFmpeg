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

%macro ANALYZE_MAC 9 ; out1, out2, in1, in2, tmp1, tmp2, add1, add2, offset
    NIDN movq,    %5, %3
    NIDN movq,    %6, %4
    pmaddwd       %5, [constsq+%9]
    pmaddwd       %6, [constsq+%9+8]
    NIDN paddd,   %1, %7
    NIDN paddd,   %2, %8
%endmacro

%macro ANALYZE_MAC_IN 7 ; out1, out2, tmp1, tmp2, add1, add2, offset
    ANALYZE_MAC   %1, %2, [inq+%7], [inq+%7+8], %3, %4, %5, %6, %7
%endmacro

%macro ANALYZE_MAC_REG 7 ; out1, out2, in, tmp1, tmp2, offset, pack
%ifidn %7, pack
    psrad         %3, 16    ; SBC_PROTO_FIXED_SCALE
    packssdw      %3, %3
%endif
    ANALYZE_MAC   %1, %2, %3, %3, %4, %5, %4, %5, %6
%endmacro

;*******************************************************************
;void ff_sbc_analyze_4(const int16_t *in, int32_t *out, const int16_t *consts);
;*******************************************************************
INIT_MMX mmx
cglobal sbc_analyze_4, 3, 3, 4, in, out, consts
    ANALYZE_MAC_IN   m0, m1, m0, m1, [scale_mask], [scale_mask], 0
    ANALYZE_MAC_IN   m0, m1, m2, m3, m2, m3, 16
    ANALYZE_MAC_IN   m0, m1, m2, m3, m2, m3, 32
    ANALYZE_MAC_IN   m0, m1, m2, m3, m2, m3, 48
    ANALYZE_MAC_IN   m0, m1, m2, m3, m2, m3, 64

    ANALYZE_MAC_REG  m0, m2, m0, m0, m2, 80, pack
    ANALYZE_MAC_REG  m0, m2, m1, m1, m3, 96, pack

    movq          [outq  ], m0
    movq          [outq+8], m2

    RET


;*******************************************************************
;void ff_sbc_analyze_8(const int16_t *in, int32_t *out, const int16_t *consts);
;*******************************************************************
INIT_MMX mmx
cglobal sbc_analyze_8, 3, 3, 4, in, out, consts
    ANALYZE_MAC_IN   m0, m1, m0, m1, [scale_mask], [scale_mask],  0
    ANALYZE_MAC_IN   m2, m3, m2, m3, [scale_mask], [scale_mask], 16
    ANALYZE_MAC_IN   m0, m1, m4, m5, m4, m5,  32
    ANALYZE_MAC_IN   m2, m3, m6, m7, m6, m7,  48
    ANALYZE_MAC_IN   m0, m1, m4, m5, m4, m5,  64
    ANALYZE_MAC_IN   m2, m3, m6, m7, m6, m7,  80
    ANALYZE_MAC_IN   m0, m1, m4, m5, m4, m5,  96
    ANALYZE_MAC_IN   m2, m3, m6, m7, m6, m7, 112
    ANALYZE_MAC_IN   m0, m1, m4, m5, m4, m5, 128
    ANALYZE_MAC_IN   m2, m3, m6, m7, m6, m7, 144

    ANALYZE_MAC_REG  m4, m5, m0, m4, m5, 160, pack
    ANALYZE_MAC_REG  m4, m5, m1, m6, m7, 192, pack
    ANALYZE_MAC_REG  m4, m5, m2, m6, m7, 224, pack
    ANALYZE_MAC_REG  m4, m5, m3, m6, m7, 256, pack

    movq          [outq  ], m4
    movq          [outq+8], m5

    ANALYZE_MAC_REG  m0, m5, m0, m0, m5, 176, no
    ANALYZE_MAC_REG  m0, m5, m1, m1, m7, 208, no
    ANALYZE_MAC_REG  m0, m5, m2, m2, m7, 240, no
    ANALYZE_MAC_REG  m0, m5, m3, m3, m7, 272, no

    movq          [outq+16], m0
    movq          [outq+24], m5

    RET


;*******************************************************************
;void ff_sbc_calc_scalefactors(int32_t sb_sample_f[16][2][8],
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
    shl           subbandsd, 1

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
