;******************************************************************************
;* SIMD optimized SAO functions for HEVC 10/12bit decoding
;*
;* Copyright (c) 2013 Pierre-Edouard LEPERE
;* Copyright (c) 2014 James Almer
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

pw_m2:     times 16 dw -2
pw_mask10: times 16 dw 0x03FF
pw_mask12: times 16 dw 0x0FFF
pb_eo:              db -1, 0, 1, 0, 0, -1, 0, 1, -1, -1, 1, 1, 1, -1, -1, 1
cextern pw_m1
cextern pw_1
cextern pw_2

SECTION .text

;******************************************************************************
;SAO Band Filter
;******************************************************************************

%macro HEVC_SAO_BAND_FILTER_INIT 1
    and            leftq, 31
    movd             xm0, leftd
    add            leftq, 1
    and            leftq, 31
    movd             xm1, leftd
    add            leftq, 1
    and            leftq, 31
    movd             xm2, leftd
    add            leftq, 1
    and            leftq, 31
    movd             xm3, leftd

    SPLATW            m0, xm0
    SPLATW            m1, xm1
    SPLATW            m2, xm2
    SPLATW            m3, xm3
%if mmsize > 16
    SPLATW            m4, [offsetq + 2]
    SPLATW            m5, [offsetq + 4]
    SPLATW            m6, [offsetq + 6]
    SPLATW            m7, [offsetq + 8]
%else
    movq              m7, [offsetq + 2]
    SPLATW            m4, m7, 0
    SPLATW            m5, m7, 1
    SPLATW            m6, m7, 2
    SPLATW            m7, m7, 3
%endif

%if ARCH_X86_64
    mova             m13, [pw_mask %+ %1]
    pxor             m14, m14

%else ; ARCH_X86_32
    mova  [rsp+mmsize*0], m0
    mova  [rsp+mmsize*1], m1
    mova  [rsp+mmsize*2], m2
    mova  [rsp+mmsize*3], m3
    mova  [rsp+mmsize*4], m4
    mova  [rsp+mmsize*5], m5
    mova  [rsp+mmsize*6], m6
    mova              m1, [pw_mask %+ %1]
    pxor              m0, m0
    %define m14 m0
    %define m13 m1
    %define  m9 m2
    %define  m8 m3
%endif ; ARCH
DEFINE_ARGS dst, src, dststride, srcstride, offset, height
    mov          heightd, r7m
%endmacro

;void ff_hevc_sao_band_filter_<width>_<depth>_<opt>(uint8_t *_dst, uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src,
;                                                   int16_t *sao_offset_val, int sao_left_class, int width, int height);
%macro HEVC_SAO_BAND_FILTER 3
cglobal hevc_sao_band_filter_%2_%1, 6, 6, 15, 7*mmsize*ARCH_X86_32, dst, src, dststride, srcstride, offset, left
    HEVC_SAO_BAND_FILTER_INIT %1

align 16
.loop:

%assign i 0
%assign j 0
%rep %3
%assign k 8+(j&1)
%assign l 9-(j&1)
    mova          m %+ k, [srcq + i]
    psraw         m %+ l, m %+ k, %1-5
%if ARCH_X86_64
    pcmpeqw          m10, m %+ l, m0
    pcmpeqw          m11, m %+ l, m1
    pcmpeqw          m12, m %+ l, m2
    pcmpeqw       m %+ l, m3
    pand             m10, m4
    pand             m11, m5
    pand             m12, m6
    pand          m %+ l, m7
    por              m10, m11
    por              m12, m %+ l
    por              m10, m12
    paddw         m %+ k, m10
%else ; ARCH_X86_32
    pcmpeqw           m4, m %+ l, [rsp+mmsize*0]
    pcmpeqw           m5, m %+ l, [rsp+mmsize*1]
    pcmpeqw           m6, m %+ l, [rsp+mmsize*2]
    pcmpeqw       m %+ l, [rsp+mmsize*3]
    pand              m4, [rsp+mmsize*4]
    pand              m5, [rsp+mmsize*5]
    pand              m6, [rsp+mmsize*6]
    pand          m %+ l, m7
    por               m4, m5
    por               m6, m %+ l
    por               m4, m6
    paddw         m %+ k, m4
%endif ; ARCH
    CLIPW             m %+ k, m14, m13
    mova      [dstq + i], m %+ k
%assign i i+mmsize
%assign j j+1
%endrep

    add             dstq, dststrideq
    add             srcq, srcstrideq
    dec          heightd
    jg .loop
    REP_RET
%endmacro

%macro HEVC_SAO_BAND_FILTER_FUNCS 0
HEVC_SAO_BAND_FILTER 10,  8, 1
HEVC_SAO_BAND_FILTER 10, 16, 2
HEVC_SAO_BAND_FILTER 10, 32, 4
HEVC_SAO_BAND_FILTER 10, 48, 6
HEVC_SAO_BAND_FILTER 10, 64, 8

HEVC_SAO_BAND_FILTER 12,  8, 1
HEVC_SAO_BAND_FILTER 12, 16, 2
HEVC_SAO_BAND_FILTER 12, 32, 4
HEVC_SAO_BAND_FILTER 12, 48, 6
HEVC_SAO_BAND_FILTER 12, 64, 8
%endmacro

INIT_XMM sse2
HEVC_SAO_BAND_FILTER_FUNCS
INIT_XMM avx
HEVC_SAO_BAND_FILTER_FUNCS

%if HAVE_AVX2_EXTERNAL
INIT_XMM avx2
HEVC_SAO_BAND_FILTER 10,  8, 1
INIT_YMM avx2
HEVC_SAO_BAND_FILTER 10, 16, 1
HEVC_SAO_BAND_FILTER 10, 32, 2
HEVC_SAO_BAND_FILTER 10, 48, 3
HEVC_SAO_BAND_FILTER 10, 64, 4

INIT_XMM avx2
HEVC_SAO_BAND_FILTER 12,  8, 1
INIT_YMM avx2
HEVC_SAO_BAND_FILTER 12, 16, 1
HEVC_SAO_BAND_FILTER 12, 32, 2
HEVC_SAO_BAND_FILTER 12, 48, 3
HEVC_SAO_BAND_FILTER 12, 64, 4
%endif

;******************************************************************************
;SAO Edge Filter
;******************************************************************************

%define MAX_PB_SIZE  64
%define PADDING_SIZE 32 ; AV_INPUT_BUFFER_PADDING_SIZE
%define EDGE_SRCSTRIDE 2 * MAX_PB_SIZE + PADDING_SIZE

%macro PMINUW 4
%if cpuflag(sse4)
    pminuw            %1, %2, %3
%else
    psubusw           %4, %2, %3
    psubw             %1, %2, %4
%endif
%endmacro

%macro HEVC_SAO_EDGE_FILTER_INIT 0
%if WIN64
    movsxd           eoq, dword eom
%elif ARCH_X86_64
    movsxd           eoq, eod
%else
    mov              eoq, r4m
%endif
    lea            tmp2q, [pb_eo]
    movsx      a_strideq, byte [tmp2q+eoq*4+1]
    movsx      b_strideq, byte [tmp2q+eoq*4+3]
    imul       a_strideq, EDGE_SRCSTRIDE >> 1
    imul       b_strideq, EDGE_SRCSTRIDE >> 1
    movsx           tmpq, byte [tmp2q+eoq*4]
    add        a_strideq, tmpq
    movsx           tmpq, byte [tmp2q+eoq*4+2]
    add        b_strideq, tmpq
%endmacro

;void ff_hevc_sao_edge_filter_<width>_<depth>_<opt>(uint8_t *_dst, uint8_t *_src, ptrdiff_t stride_dst, int16_t *sao_offset_val,
;                                                   int eo, int width, int height);
%macro HEVC_SAO_EDGE_FILTER 3
%if ARCH_X86_64
cglobal hevc_sao_edge_filter_%2_%1, 4, 9, 16, dst, src, dststride, offset, eo, a_stride, b_stride, height, tmp
%define tmp2q heightq
    HEVC_SAO_EDGE_FILTER_INIT
    mov          heightd, r6m
    add        a_strideq, a_strideq
    add        b_strideq, b_strideq

%else ; ARCH_X86_32
cglobal hevc_sao_edge_filter_%2_%1, 1, 6, 8, 5*mmsize, dst, src, dststride, a_stride, b_stride, height
%define eoq   srcq
%define tmpq  heightq
%define tmp2q dststrideq
%define offsetq heightq
%define m8 m1
%define m9 m2
%define m10 m3
%define m11 m4
%define m12 m5
    HEVC_SAO_EDGE_FILTER_INIT
    mov             srcq, srcm
    mov          offsetq, r3m
    mov       dststrideq, dststridem
    add        a_strideq, a_strideq
    add        b_strideq, b_strideq

%endif ; ARCH

%if mmsize > 16
    SPLATW            m8, [offsetq+2]
    SPLATW            m9, [offsetq+4]
    SPLATW           m10, [offsetq+0]
    SPLATW           m11, [offsetq+6]
    SPLATW           m12, [offsetq+8]
%else
    movq             m10, [offsetq+0]
    movd             m12, [offsetq+6]
    SPLATW            m8, xm10, 1
    SPLATW            m9, xm10, 2
    SPLATW           m10, xm10, 0
    SPLATW           m11, xm12, 0
    SPLATW           m12, xm12, 1
%endif
    pxor              m0, m0
%if ARCH_X86_64
    mova             m13, [pw_m1]
    mova             m14, [pw_1]
    mova             m15, [pw_2]
%else
    mov          heightd, r6m
    mova  [rsp+mmsize*0], m8
    mova  [rsp+mmsize*1], m9
    mova  [rsp+mmsize*2], m10
    mova  [rsp+mmsize*3], m11
    mova  [rsp+mmsize*4], m12
%endif

align 16
.loop:

%assign i 0
%rep %3
    mova              m1, [srcq + i]
    movu              m2, [srcq+a_strideq + i]
    movu              m3, [srcq+b_strideq + i]
    PMINUW            m4, m1, m2, m6
    PMINUW            m5, m1, m3, m7
    pcmpeqw           m2, m4
    pcmpeqw           m3, m5
    pcmpeqw           m4, m1
    pcmpeqw           m5, m1
    psubw             m4, m2
    psubw             m5, m3

    paddw             m4, m5
    pcmpeqw           m2, m4, [pw_m2]
%if ARCH_X86_64
    pcmpeqw           m3, m4, m13
    pcmpeqw           m5, m4, m0
    pcmpeqw           m6, m4, m14
    pcmpeqw           m7, m4, m15
    pand              m2, m8
    pand              m3, m9
    pand              m5, m10
    pand              m6, m11
    pand              m7, m12
%else
    pcmpeqw           m3, m4, [pw_m1]
    pcmpeqw           m5, m4, m0
    pcmpeqw           m6, m4, [pw_1]
    pcmpeqw           m7, m4, [pw_2]
    pand              m2, [rsp+mmsize*0]
    pand              m3, [rsp+mmsize*1]
    pand              m5, [rsp+mmsize*2]
    pand              m6, [rsp+mmsize*3]
    pand              m7, [rsp+mmsize*4]
%endif
    paddw             m2, m3
    paddw             m5, m6
    paddw             m2, m7
    paddw             m2, m1
    paddw             m2, m5
    CLIPW             m2, m0, [pw_mask %+ %1]
    mova      [dstq + i], m2
%assign i i+mmsize
%endrep

    add             dstq, dststrideq
    add             srcq, EDGE_SRCSTRIDE
    dec          heightd
    jg .loop
    RET
%endmacro

INIT_XMM sse2
HEVC_SAO_EDGE_FILTER 10,  8, 1
HEVC_SAO_EDGE_FILTER 10, 16, 2
HEVC_SAO_EDGE_FILTER 10, 32, 4
HEVC_SAO_EDGE_FILTER 10, 48, 6
HEVC_SAO_EDGE_FILTER 10, 64, 8

HEVC_SAO_EDGE_FILTER 12,  8, 1
HEVC_SAO_EDGE_FILTER 12, 16, 2
HEVC_SAO_EDGE_FILTER 12, 32, 4
HEVC_SAO_EDGE_FILTER 12, 48, 6
HEVC_SAO_EDGE_FILTER 12, 64, 8

%if HAVE_AVX2_EXTERNAL
INIT_XMM avx2
HEVC_SAO_EDGE_FILTER 10,  8, 1
INIT_YMM avx2
HEVC_SAO_EDGE_FILTER 10, 16, 1
HEVC_SAO_EDGE_FILTER 10, 32, 2
HEVC_SAO_EDGE_FILTER 10, 48, 3
HEVC_SAO_EDGE_FILTER 10, 64, 4

INIT_XMM avx2
HEVC_SAO_EDGE_FILTER 12,  8, 1
INIT_YMM avx2
HEVC_SAO_EDGE_FILTER 12, 16, 1
HEVC_SAO_EDGE_FILTER 12, 32, 2
HEVC_SAO_EDGE_FILTER 12, 48, 3
HEVC_SAO_EDGE_FILTER 12, 64, 4
%endif
