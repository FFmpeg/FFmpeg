;******************************************************************************
;* SIMD-optimized UTVideo functions
;* Copyright (c) 2017 Paul B Mahol
;* Copyright (c) 2017 Jokyo Images
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

cextern pb_80
cextern pw_512
cextern pw_1023

SECTION .text

;-------------------------------------------------------------------------------------------
; void restore_rgb_planes(uint8_t *src_r, uint8_t *src_g, uint8_t *src_b,
;                         ptrdiff_t linesize_r, ptrdiff_t linesize_g, ptrdiff_t linesize_b,
;                         int width, int height)
;-------------------------------------------------------------------------------------------
%macro RESTORE_RGB_PLANES 0
cglobal restore_rgb_planes, 7 + ARCH_X86_64, 7 + ARCH_X86_64 * 2, 4, src_r, src_g, src_b, linesize_r, linesize_g, linesize_b, w, h, x
    movsxdifnidn wq, wd
    add      src_rq, wq
    add      src_gq, wq
    add      src_bq, wq
    neg          wq
%if ARCH_X86_64 == 0
    mov          wm, wq
DEFINE_ARGS src_r, src_g, src_b, linesize_r, linesize_g, linesize_b, x
%define wq r6m
%define hd r7mp
%endif
    mova         m3, [pb_80]
.nextrow:
    mov          xq, wq

    .loop:
        mova           m0, [src_rq + xq]
        mova           m1, [src_gq + xq]
        mova           m2, [src_bq + xq]
        psubb          m1, m3
        paddb          m0, m1
        paddb          m2, m1
        mova  [src_rq+xq], m0
        mova  [src_bq+xq], m2
        add            xq, mmsize
    jl .loop

    add        src_rq, linesize_rq
    add        src_gq, linesize_gq
    add        src_bq, linesize_bq
    sub        hd, 1
    jg .nextrow
    REP_RET
%endmacro

INIT_XMM sse2
RESTORE_RGB_PLANES

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
RESTORE_RGB_PLANES
%endif

;-------------------------------------------------------------------------------------------
; void restore_rgb_planes10(uint16_t *src_r, uint16_t *src_g, uint16_t *src_b,
;                         ptrdiff_t linesize_r, ptrdiff_t linesize_g, ptrdiff_t linesize_b,
;                         int width, int height)
;-------------------------------------------------------------------------------------------
%macro RESTORE_RGB_PLANES10 0
cglobal restore_rgb_planes10, 7 + ARCH_X86_64, 7 + ARCH_X86_64 * 2, 5, src_r, src_g, src_b, linesize_r, linesize_g, linesize_b, w, h, x
    shl          wd, 1
    shl linesize_rq, 1
    shl linesize_gq, 1
    shl linesize_bq, 1
    add      src_rq, wq
    add      src_gq, wq
    add      src_bq, wq
    mova         m3, [pw_512]
    mova         m4, [pw_1023]
    neg          wq
%if ARCH_X86_64 == 0
    mov          wm, wq
DEFINE_ARGS src_r, src_g, src_b, linesize_r, linesize_g, linesize_b, x
%define wq r6m
%define hd r7mp
%endif
.nextrow:
    mov          xq, wq

    .loop:
        mova           m0, [src_rq + xq]
        mova           m1, [src_gq + xq]
        mova           m2, [src_bq + xq]
        psubw          m1, m3
        paddw          m0, m1
        paddw          m2, m1
        pand           m0, m4
        pand           m2, m4
        mova  [src_rq+xq], m0
        mova  [src_bq+xq], m2
        add            xq, mmsize
    jl .loop

    add        src_rq, linesize_rq
    add        src_gq, linesize_gq
    add        src_bq, linesize_bq
    sub        hd, 1
    jg .nextrow
    REP_RET
%endmacro

INIT_XMM sse2
RESTORE_RGB_PLANES10

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
RESTORE_RGB_PLANES10
%endif
