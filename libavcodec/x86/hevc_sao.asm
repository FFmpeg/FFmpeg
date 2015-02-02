;******************************************************************************
;* SIMD optimized SAO functions for HEVC decoding
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

%if ARCH_X86_64
SECTION_RODATA 32

pw_mask10: times 16 dw 0x03FF
pw_mask12: times 16 dw 0x0FFF

SECTION_TEXT

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

%if %1 > 8
    mova             m13, [pw_mask %+ %1]
%endif
    pxor             m14, m14

DEFINE_ARGS dst, src, dststride, srcstride, offset, height
    mov          heightd, r7m
%endmacro

%macro HEVC_SAO_BAND_FILTER_COMPUTE 3
    psraw             %2, %3, %1-5
    pcmpeqw          m10, %2, m0
    pcmpeqw          m11, %2, m1
    pcmpeqw          m12, %2, m2
    pcmpeqw           %2, m3
    pand             m10, m4
    pand             m11, m5
    pand             m12, m6
    pand              %2, m7
    por              m10, m11
    por              m12, %2
    por              m10, m12
    paddw             %3, m10
%endmacro

;void ff_hevc_sao_band_filter_<width>_8_<opt>(uint8_t *_dst, uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src,
;                                             int16_t *sao_offset_val, int sao_left_class, int width, int height);
%macro HEVC_SAO_BAND_FILTER_8 2
cglobal hevc_sao_band_filter_%1_8, 6, 6, 15, dst, src, dststride, srcstride, offset, left
    HEVC_SAO_BAND_FILTER_INIT 8

align 16
.loop
%if %1 == 8
    movq              m8, [srcq]
    punpcklbw         m8, m14
    HEVC_SAO_BAND_FILTER_COMPUTE 8, m9, m8
    packuswb          m8, m14
    movq          [dstq], m8
%endif ; %1 == 8

%assign i 0
%rep %2
    mova             m13, [srcq + i]
    punpcklbw         m8, m13, m14
    HEVC_SAO_BAND_FILTER_COMPUTE 8, m9,  m8
    punpckhbw        m13, m14
    HEVC_SAO_BAND_FILTER_COMPUTE 8, m9, m13
    packuswb          m8, m13
    mova      [dstq + i], m8
%assign i i+mmsize
%endrep

%if %1 == 48
INIT_XMM cpuname

    mova             m13, [srcq + i]
    punpcklbw         m8, m13, m14
    HEVC_SAO_BAND_FILTER_COMPUTE 8, m9,  m8
    punpckhbw        m13, m14
    HEVC_SAO_BAND_FILTER_COMPUTE 8, m9, m13
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

;void ff_hevc_sao_band_filter_<width>_<depth>_<opt>(uint8_t *_dst, uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src,
;                                                   int16_t *sao_offset_val, int sao_left_class, int width, int height);
%macro HEVC_SAO_BAND_FILTER_16 3
cglobal hevc_sao_band_filter_%2_%1, 6, 6, 15, dst, src, dststride, srcstride, offset, left
    HEVC_SAO_BAND_FILTER_INIT %1

align 16
.loop
%if %2 == 8
    mova              m8, [srcq]
    HEVC_SAO_BAND_FILTER_COMPUTE %1, m9, m8
    CLIPW             m8, m14, m13
    mova          [dstq], m8
%endif

%assign i 0
%rep %3
    mova              m8, [srcq + i]
    HEVC_SAO_BAND_FILTER_COMPUTE %1, m9, m8
    CLIPW             m8, m14, m13
    mova      [dstq + i], m8

    mova              m9, [srcq + i + mmsize]
    HEVC_SAO_BAND_FILTER_COMPUTE %1, m8, m9
    CLIPW             m9, m14, m13
    mova      [dstq + i + mmsize], m9
%assign i i+mmsize*2
%endrep

%if %2 == 48
INIT_XMM cpuname
    mova              m8, [srcq + i]
    HEVC_SAO_BAND_FILTER_COMPUTE %1, m9, m8
    CLIPW             m8, m14, m13
    mova      [dstq + i], m8

    mova              m9, [srcq + i + mmsize]
    HEVC_SAO_BAND_FILTER_COMPUTE %1, m8, m9
    CLIPW             m9, m14, m13
    mova      [dstq + i + mmsize], m9
%if cpuflag(avx2)
INIT_YMM cpuname
%endif
%endif ; %1 == 48

    add             dstq, dststrideq
    add             srcq, srcstrideq
    dec          heightd
    jg .loop
    REP_RET
%endmacro

%macro HEVC_SAO_BAND_FILTER_FUNCS 0
HEVC_SAO_BAND_FILTER_8       8, 0
HEVC_SAO_BAND_FILTER_8      16, 1
HEVC_SAO_BAND_FILTER_8      32, 2
HEVC_SAO_BAND_FILTER_8      48, 2
HEVC_SAO_BAND_FILTER_8      64, 4

HEVC_SAO_BAND_FILTER_16 10,  8, 0
HEVC_SAO_BAND_FILTER_16 10, 16, 1
HEVC_SAO_BAND_FILTER_16 10, 32, 2
HEVC_SAO_BAND_FILTER_16 10, 48, 2
HEVC_SAO_BAND_FILTER_16 10, 64, 4

HEVC_SAO_BAND_FILTER_16 12,  8, 0
HEVC_SAO_BAND_FILTER_16 12, 16, 1
HEVC_SAO_BAND_FILTER_16 12, 32, 2
HEVC_SAO_BAND_FILTER_16 12, 48, 2
HEVC_SAO_BAND_FILTER_16 12, 64, 4
%endmacro

INIT_XMM sse2
HEVC_SAO_BAND_FILTER_FUNCS
INIT_XMM avx
HEVC_SAO_BAND_FILTER_FUNCS

%if HAVE_AVX2_EXTERNAL
INIT_XMM avx2
HEVC_SAO_BAND_FILTER_8       8, 0
HEVC_SAO_BAND_FILTER_8      16, 1
INIT_YMM avx2
HEVC_SAO_BAND_FILTER_8      32, 1
HEVC_SAO_BAND_FILTER_8      48, 1
HEVC_SAO_BAND_FILTER_8      64, 2

INIT_XMM avx2
HEVC_SAO_BAND_FILTER_16 10,  8, 0
HEVC_SAO_BAND_FILTER_16 10, 16, 1
INIT_YMM avx2
HEVC_SAO_BAND_FILTER_16 10, 32, 1
HEVC_SAO_BAND_FILTER_16 10, 48, 1
HEVC_SAO_BAND_FILTER_16 10, 64, 2

INIT_XMM avx2
HEVC_SAO_BAND_FILTER_16 12,  8, 0
HEVC_SAO_BAND_FILTER_16 12, 16, 1
INIT_YMM avx2
HEVC_SAO_BAND_FILTER_16 12, 32, 1
HEVC_SAO_BAND_FILTER_16 12, 48, 1
HEVC_SAO_BAND_FILTER_16 12, 64, 2
%endif
%endif
