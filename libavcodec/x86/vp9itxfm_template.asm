;******************************************************************************
;* VP9 IDCT SIMD optimizations
;*
;* Copyright (C) 2013 Clément Bœsch <u pkh me>
;* Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
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

%macro VP9_IWHT4_1D 0
    SWAP                 1, 2, 3
    paddw               m0, m2
    psubw               m3, m1
    psubw               m4, m0, m3
    psraw               m4, 1
    psubw               m5, m4, m1
    SWAP                 5, 1
    psubw               m4, m2
    SWAP                 4, 2
    psubw               m0, m1
    paddw               m3, m2
    SWAP                 3, 2, 1
%endmacro
