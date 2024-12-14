;******************************************************************************
;* SIMD optimized SAO functions for VVC 8bit decoding
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
%include "libavcodec/x86/h26x/h2656_sao.asm"

%macro VVC_SAO_BAND_FILTER 2
    H2656_SAO_BAND_FILTER vvc, %1, %2
%endmacro

%macro VVC_SAO_BAND_FILTER_FUNCS 0
VVC_SAO_BAND_FILTER   8, 0
VVC_SAO_BAND_FILTER  16, 1
VVC_SAO_BAND_FILTER  32, 2
VVC_SAO_BAND_FILTER  48, 2
VVC_SAO_BAND_FILTER  64, 4
VVC_SAO_BAND_FILTER  80, 4
VVC_SAO_BAND_FILTER  96, 6
VVC_SAO_BAND_FILTER 112, 6
VVC_SAO_BAND_FILTER 128, 8
%endmacro

%if HAVE_AVX2_EXTERNAL
INIT_XMM avx2
VVC_SAO_BAND_FILTER   8, 0
VVC_SAO_BAND_FILTER  16, 1
INIT_YMM avx2
VVC_SAO_BAND_FILTER  32, 1
VVC_SAO_BAND_FILTER  48, 1
VVC_SAO_BAND_FILTER  64, 2
VVC_SAO_BAND_FILTER  80, 2
VVC_SAO_BAND_FILTER  96, 3
VVC_SAO_BAND_FILTER 112, 3
VVC_SAO_BAND_FILTER 128, 4
%endif

%macro VVC_SAO_EDGE_FILTER 2-3
    H2656_SAO_EDGE_FILTER vvc, %{1:-1}
%endmacro

%if HAVE_AVX2_EXTERNAL
INIT_XMM avx2
VVC_SAO_EDGE_FILTER  8, 0
VVC_SAO_EDGE_FILTER 16, 1, a
INIT_YMM avx2
VVC_SAO_EDGE_FILTER  32, 1, a
VVC_SAO_EDGE_FILTER  48, 1, u
VVC_SAO_EDGE_FILTER  64, 2, a
VVC_SAO_EDGE_FILTER  80, 2, u
VVC_SAO_EDGE_FILTER  96, 3, a
VVC_SAO_EDGE_FILTER 112, 3, u
VVC_SAO_EDGE_FILTER 128, 4, a
%endif
