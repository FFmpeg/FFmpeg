;******************************************************************************
;* SIMD optimized SAO functions for HEVC 8bit decoding
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

pb_edge_shuffle: times 2 db 1, 2, 0, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
pb_eo:                   db -1, 0, 1, 0, 0, -1, 0, 1, -1, -1, 1, 1, 1, -1, -1, 1
cextern pb_1
cextern pb_2

SECTION .text

;******************************************************************************
;SAO Band Filter
;******************************************************************************

%macro HEVC_SAO_BAND_FILTER_INIT 0
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
    pxor             m14, m14

%else ; ARCH_X86_32
    mova  [rsp+mmsize*0], m0
    mova  [rsp+mmsize*1], m1
    mova  [rsp+mmsize*2], m2
    mova  [rsp+mmsize*3], m3
    mova  [rsp+mmsize*4], m4
    mova  [rsp+mmsize*5], m5
    mova  [rsp+mmsize*6], m6
    pxor              m0, m0
    %assign MMSIZE mmsize
    %define m14 m0
    %define m13 m1
    %define  m9 m2
    %define  m8 m3
%endif ; ARCH
DEFINE_ARGS dst, src, dststride, srcstride, offset, height
    mov          heightd, r7m
%endmacro

%macro HEVC_SAO_BAND_FILTER_COMPUTE 2
    psraw             %1, %2, 3
%if ARCH_X86_64
    pcmpeqw          m10, %1, m0
    pcmpeqw          m11, %1, m1
    pcmpeqw          m12, %1, m2
    pcmpeqw           %1, m3
    pand             m10, m4
    pand             m11, m5
    pand             m12, m6
    pand              %1, m7
    por              m10, m11
    por              m12, %1
    por              m10, m12
    paddw             %2, m10
%else ; ARCH_X86_32
    pcmpeqw           m4, %1, [rsp+MMSIZE*0]
    pcmpeqw           m5, %1, [rsp+MMSIZE*1]
    pcmpeqw           m6, %1, [rsp+MMSIZE*2]
    pcmpeqw           %1, [rsp+MMSIZE*3]
    pand              m4, [rsp+MMSIZE*4]
    pand              m5, [rsp+MMSIZE*5]
    pand              m6, [rsp+MMSIZE*6]
    pand              %1, m7
    por               m4, m5
    por               m6, %1
    por               m4, m6
    paddw             %2, m4
%endif ; ARCH
%endmacro

;void ff_hevc_sao_band_filter_<width>_8_<opt>(uint8_t *_dst, uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src,
;                                             int16_t *sao_offset_val, int sao_left_class, int width, int height);
%macro HEVC_SAO_BAND_FILTER 2
cglobal hevc_sao_band_filter_%1_8, 6, 6, 15, 7*mmsize*ARCH_X86_32, dst, src, dststride, srcstride, offset, left
    HEVC_SAO_BAND_FILTER_INIT

align 16
.loop:
%if %1 == 8
    movq              m8, [srcq]
    punpcklbw         m8, m14
    HEVC_SAO_BAND_FILTER_COMPUTE m9, m8
    packuswb          m8, m14
    movq          [dstq], m8
%endif ; %1 == 8

%assign i 0
%rep %2
    mova             m13, [srcq + i]
    punpcklbw         m8, m13, m14
    HEVC_SAO_BAND_FILTER_COMPUTE m9,  m8
    punpckhbw        m13, m14
    HEVC_SAO_BAND_FILTER_COMPUTE m9, m13
    packuswb          m8, m13
    mova      [dstq + i], m8
%assign i i+mmsize
%endrep

%if %1 == 48
INIT_XMM cpuname

    mova             m13, [srcq + i]
    punpcklbw         m8, m13, m14
    HEVC_SAO_BAND_FILTER_COMPUTE m9,  m8
    punpckhbw        m13, m14
    HEVC_SAO_BAND_FILTER_COMPUTE m9, m13
    packuswb          m8, m13
    mova      [dstq + i], m8
%if cpuflag(avx2)
INIT_YMM cpuname
%endif
%endif ; %1 == 48

    add             dstq, dststrideq             ; dst += dststride
    add             srcq, srcstrideq             ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
    REP_RET
%endmacro


%macro HEVC_SAO_BAND_FILTER_FUNCS 0
HEVC_SAO_BAND_FILTER  8, 0
HEVC_SAO_BAND_FILTER 16, 1
HEVC_SAO_BAND_FILTER 32, 2
HEVC_SAO_BAND_FILTER 48, 2
HEVC_SAO_BAND_FILTER 64, 4
%endmacro

INIT_XMM sse2
HEVC_SAO_BAND_FILTER_FUNCS
INIT_XMM avx
HEVC_SAO_BAND_FILTER_FUNCS

%if HAVE_AVX2_EXTERNAL
INIT_XMM avx2
HEVC_SAO_BAND_FILTER  8, 0
HEVC_SAO_BAND_FILTER 16, 1
INIT_YMM avx2
HEVC_SAO_BAND_FILTER 32, 1
HEVC_SAO_BAND_FILTER 48, 1
HEVC_SAO_BAND_FILTER 64, 2
%endif

;******************************************************************************
;SAO Edge Filter
;******************************************************************************

%define MAX_PB_SIZE  64
%define PADDING_SIZE 64 ; AV_INPUT_BUFFER_PADDING_SIZE
%define EDGE_SRCSTRIDE 2 * MAX_PB_SIZE + PADDING_SIZE

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
    imul       a_strideq, EDGE_SRCSTRIDE
    imul       b_strideq, EDGE_SRCSTRIDE
    movsx           tmpq, byte [tmp2q+eoq*4]
    add        a_strideq, tmpq
    movsx           tmpq, byte [tmp2q+eoq*4+2]
    add        b_strideq, tmpq
%endmacro

%macro HEVC_SAO_EDGE_FILTER_COMPUTE 1
    pminub            m4, m1, m2
    pminub            m5, m1, m3
    pcmpeqb           m2, m4
    pcmpeqb           m3, m5
    pcmpeqb           m4, m1
    pcmpeqb           m5, m1
    psubb             m4, m2
    psubb             m5, m3
    paddb             m4, m6
    paddb             m4, m5

    pshufb            m2, m0, m4
%if %1 > 8
    punpckhbw         m5, m7, m1
    punpckhbw         m4, m2, m7
    punpcklbw         m3, m7, m1
    punpcklbw         m2, m7
    pmaddubsw         m5, m4
    pmaddubsw         m3, m2
    packuswb          m3, m5
%else
    punpcklbw         m3, m7, m1
    punpcklbw         m2, m7
    pmaddubsw         m3, m2
    packuswb          m3, m3
%endif
%endmacro

;void ff_hevc_sao_edge_filter_<width>_8_<opt>(uint8_t *_dst, uint8_t *_src, ptrdiff_t stride_dst, int16_t *sao_offset_val,
;                                             int eo, int width, int height);
%macro HEVC_SAO_EDGE_FILTER 2-3
%if ARCH_X86_64
cglobal hevc_sao_edge_filter_%1_8, 4, 9, 8, dst, src, dststride, offset, eo, a_stride, b_stride, height, tmp
%define tmp2q heightq
    HEVC_SAO_EDGE_FILTER_INIT
    mov          heightd, r6m

%else ; ARCH_X86_32
cglobal hevc_sao_edge_filter_%1_8, 1, 6, 8, dst, src, dststride, a_stride, b_stride, height
%define eoq   srcq
%define tmpq  heightq
%define tmp2q dststrideq
%define offsetq heightq
    HEVC_SAO_EDGE_FILTER_INIT
    mov             srcq, srcm
    mov          offsetq, r3m
    mov       dststrideq, dststridem
%endif ; ARCH

%if mmsize > 16
    vbroadcasti128    m0, [offsetq]
%else
    movu              m0, [offsetq]
%endif
    mova              m1, [pb_edge_shuffle]
    packsswb          m0, m0
    mova              m7, [pb_1]
    pshufb            m0, m1
    mova              m6, [pb_2]
%if ARCH_X86_32
    mov          heightd, r6m
%endif

align 16
.loop:

%if %1 == 8
    movq              m1, [srcq]
    movq              m2, [srcq + a_strideq]
    movq              m3, [srcq + b_strideq]
    HEVC_SAO_EDGE_FILTER_COMPUTE %1
    movq          [dstq], m3
%endif

%assign i 0
%rep %2
    mova              m1, [srcq + i]
    movu              m2, [srcq + a_strideq + i]
    movu              m3, [srcq + b_strideq + i]
    HEVC_SAO_EDGE_FILTER_COMPUTE %1
    mov%3     [dstq + i], m3
%assign i i+mmsize
%endrep

%if %1 == 48
INIT_XMM cpuname

    mova              m1, [srcq + i]
    movu              m2, [srcq + a_strideq + i]
    movu              m3, [srcq + b_strideq + i]
    HEVC_SAO_EDGE_FILTER_COMPUTE %1
    mova      [dstq + i], m3
%if cpuflag(avx2)
INIT_YMM cpuname
%endif
%endif

    add             dstq, dststrideq
    add             srcq, EDGE_SRCSTRIDE
    dec          heightd
    jg .loop
    RET
%endmacro

INIT_XMM ssse3
HEVC_SAO_EDGE_FILTER  8, 0
HEVC_SAO_EDGE_FILTER 16, 1, a
HEVC_SAO_EDGE_FILTER 32, 2, a
HEVC_SAO_EDGE_FILTER 48, 2, a
HEVC_SAO_EDGE_FILTER 64, 4, a

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
HEVC_SAO_EDGE_FILTER 32, 1, a
HEVC_SAO_EDGE_FILTER 48, 1, u
HEVC_SAO_EDGE_FILTER 64, 2, a
%endif
