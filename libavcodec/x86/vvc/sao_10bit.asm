;******************************************************************************
;* SIMD optimized SAO functions for VVC 10/12bit decoding
;*
;* Copyright (c) 2024 Shaun Loo
;* Copyright (c) 2024 Nuo Mi
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

%define MAX_PB_SIZE  128
%include "libavcodec/x86/h26x/h2656_sao_10bit.asm"

%macro VVC_SAO_BAND_FILTER 3
    H2656_SAO_BAND_FILTER vvc, %1, %2, %3
%endmacro

%macro VVC_SAO_BAND_FILTER_FUNCS 1
    VVC_SAO_BAND_FILTER %1,   8,  1
    VVC_SAO_BAND_FILTER %1,  16,  2
    VVC_SAO_BAND_FILTER %1,  32,  4
    VVC_SAO_BAND_FILTER %1,  48,  6
    VVC_SAO_BAND_FILTER %1,  64,  8
    VVC_SAO_BAND_FILTER %1,  80, 10
    VVC_SAO_BAND_FILTER %1,  96, 12
    VVC_SAO_BAND_FILTER %1, 112, 14
    VVC_SAO_BAND_FILTER %1, 128, 16
%endmacro

%macro VVC_SAO_BAND_FILTER_FUNCS 0
    VVC_SAO_BAND_FILTER_FUNCS 10
    VVC_SAO_BAND_FILTER_FUNCS 12
%endmacro

INIT_XMM sse2
VVC_SAO_BAND_FILTER_FUNCS
INIT_XMM avx
VVC_SAO_BAND_FILTER_FUNCS

%if HAVE_AVX2_EXTERNAL

%macro VVC_SAO_BAND_FILTER_FUNCS_AVX2 1
    INIT_XMM avx2
    VVC_SAO_BAND_FILTER %1,   8, 1
    INIT_YMM avx2
    VVC_SAO_BAND_FILTER %1,  16, 1
    VVC_SAO_BAND_FILTER %1,  32, 2
    VVC_SAO_BAND_FILTER %1,  48, 3
    VVC_SAO_BAND_FILTER %1,  64, 4
    VVC_SAO_BAND_FILTER %1,  80, 5
    VVC_SAO_BAND_FILTER %1,  96, 6
    VVC_SAO_BAND_FILTER %1, 112, 7
    VVC_SAO_BAND_FILTER %1, 128, 8
%endmacro

VVC_SAO_BAND_FILTER_FUNCS_AVX2 10
VVC_SAO_BAND_FILTER_FUNCS_AVX2 12

%endif ; HAVE_AVX2_EXTERNAL

%macro VVC_SAO_EDGE_FILTER 3
    H2656_SAO_EDGE_FILTER vvc, %1, %2, %3
%endmacro

%macro VVC_SAO_EDGE_FILTER_FUNCS 1
    VVC_SAO_EDGE_FILTER %1,   8,  1
    VVC_SAO_EDGE_FILTER %1,  16,  2
    VVC_SAO_EDGE_FILTER %1,  32,  4
    VVC_SAO_EDGE_FILTER %1,  48,  6
    VVC_SAO_EDGE_FILTER %1,  64,  8
    VVC_SAO_EDGE_FILTER %1,  80, 10
    VVC_SAO_EDGE_FILTER %1,  96, 12
    VVC_SAO_EDGE_FILTER %1, 112, 14
    VVC_SAO_EDGE_FILTER %1, 128, 16
%endmacro

INIT_XMM sse2
VVC_SAO_EDGE_FILTER_FUNCS 10
VVC_SAO_EDGE_FILTER_FUNCS 12

%if HAVE_AVX2_EXTERNAL

%macro VVC_SAO_EDGE_FILTER_FUNCS_AVX2 1
    INIT_XMM avx2
    VVC_SAO_EDGE_FILTER %1,   8, 1
    INIT_YMM avx2
    VVC_SAO_EDGE_FILTER %1,  16, 1
    VVC_SAO_EDGE_FILTER %1,  32, 2
    VVC_SAO_EDGE_FILTER %1,  48, 3
    VVC_SAO_EDGE_FILTER %1,  64, 4
    VVC_SAO_EDGE_FILTER %1,  80, 5
    VVC_SAO_EDGE_FILTER %1,  96, 6
    VVC_SAO_EDGE_FILTER %1, 112, 7
    VVC_SAO_EDGE_FILTER %1, 128, 8
%endmacro

VVC_SAO_EDGE_FILTER_FUNCS_AVX2 10
VVC_SAO_EDGE_FILTER_FUNCS_AVX2 12

%endif ; HAVE_AVX2_EXTERNAL
