;******************************************************************************
;* Copyright (c) 2012 Loren Merritt
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

SECTION .text

%macro LOWPASS 3 ; prevsample, cursample, lut
    sub    %1q, %2q
%if lut_bits != 8
    sar    %1q, 8-lut_bits
%endif
    movsx  %1q, word [%3q+%1q*2]
    add    %1q, %2q
%endmacro

%macro LOAD 3 ; dstreg, x, bitdepth
%if %3 == 8
    movzx  %1, byte [srcq+%2]
%else
    movzx  %1, word [srcq+(%2)*2]
%endif
%if %3 != 16
    shl    %1, 16-%3
    add    %1, (1<<(15-%3))-1
%endif
%endmacro

%macro HQDN3D_ROW 1 ; bitdepth
%if ARCH_X86_64
cglobal hqdn3d_row_%1_x86, 7,10,0, src, dst, lineant, frameant, width, spatial, temporal, pixelant, t0, t1
%else
cglobal hqdn3d_row_%1_x86, 7,7,0, src, dst, lineant, frameant, width, spatial, temporal
%endif
    %assign bytedepth (%1+7)>>3
    %assign lut_bits 4+4*(%1/16)
    dec    widthq
    lea    srcq, [srcq+widthq*bytedepth]
    lea    dstq, [dstq+widthq*bytedepth]
    lea    frameantq, [frameantq+widthq*2]
    lea    lineantq,  [lineantq+widthq*2]
    neg    widthq
    %define xq widthq
%if ARCH_X86_32
    mov    dstmp, dstq
    mov    srcmp, srcq
    mov    frameantmp, frameantq
    mov    lineantmp,  lineantq
    %define dstq r0
    %define frameantq r0
    %define lineantq  r0
    %define pixelantq r1
    %define pixelantd r1d
    DECLARE_REG_TMP 2,3
%endif
    LOAD   pixelantd, xq, %1
ALIGN 16
.loop:
    movifnidn srcq, srcmp
    LOAD      t0d, xq+1, %1 ; skip on the last iteration to avoid overread
.loop2:
    movifnidn lineantq, lineantmp
    movzx     t1d, word [lineantq+xq*2]
    LOWPASS   t1, pixelant, spatial
    mov       [lineantq+xq*2], t1w
    LOWPASS   pixelant, t0, spatial
    movifnidn frameantq, frameantmp
    movzx     t0d, word [frameantq+xq*2]
    LOWPASS   t0, t1, temporal
    mov       [frameantq+xq*2], t0w
    movifnidn dstq, dstmp
%if %1 != 16
    shr    t0d, 16-%1 ; could eliminate this by storing from t0h, but only with some contraints on register allocation
%endif
%if %1 == 8
    mov    [dstq+xq], t0b
%else
    mov    [dstq+xq*2], t0w
%endif
    inc    xq
    jl .loop
    je .loop2
    RET
%endmacro ; HQDN3D_ROW

HQDN3D_ROW 8
HQDN3D_ROW 9
HQDN3D_ROW 10
HQDN3D_ROW 16
