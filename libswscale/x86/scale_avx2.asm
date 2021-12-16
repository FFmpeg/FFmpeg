;******************************************************************************
;* x86-optimized horizontal line scaling functions
;* Copyright 2020 Google LLC
;* Copyright (c) 2011 Ronald S. Bultje <rsbultje@gmail.com>
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

swizzle: dd 0, 4, 1, 5, 2, 6, 3, 7
four: times 8 dd 4

SECTION .text

;-----------------------------------------------------------------------------
; horizontal line scaling
;
; void hscale8to15_<filterSize>_<opt>
;                   (SwsContext *c, int16_t *dst,
;                    int dstW, const uint8_t *src,
;                    const int16_t *filter,
;                    const int32_t *filterPos, int filterSize);
;
; Scale one horizontal line. Input is 8-bit width Filter is 14 bits. Output is
; 15 bits (in int16_t). Each output pixel is generated from $filterSize input
; pixels, the position of the first pixel is given in filterPos[nOutputPixel].
;-----------------------------------------------------------------------------

%macro SCALE_FUNC 1
cglobal hscale8to15_%1, 7, 9, 16, pos0, dst, w, srcmem, filter, fltpos, fltsize, count, inner
    pxor m0, m0
    mova m15, [swizzle]
    xor countq, countq
    movsxd wq, wd
%ifidn %1, X4
    mova m14, [four]
    shr fltsized, 2
%endif
.loop:
    movu m1, [fltposq]
    movu m2, [fltposq+32]
%ifidn %1, X4
    pxor m9, m9
    pxor m10, m10
    pxor m11, m11
    pxor m12, m12
    xor innerq, innerq
.innerloop:
%endif
    vpcmpeqd  m13, m13
    vpgatherdd m3,[srcmemq + m1], m13
    vpcmpeqd  m13, m13
    vpgatherdd m4,[srcmemq + m2], m13
    vpunpcklbw m5, m3, m0
    vpunpckhbw m6, m3, m0
    vpunpcklbw m7, m4, m0
    vpunpckhbw m8, m4, m0
    vpmaddwd m5, m5, [filterq]
    vpmaddwd m6, m6, [filterq + 32]
    vpmaddwd m7, m7, [filterq + 64]
    vpmaddwd m8, m8, [filterq + 96]
    add filterq, 0x80
%ifidn %1, X4
    paddd m9, m5
    paddd m10, m6
    paddd m11, m7
    paddd m12, m8
    paddd m1, m14
    paddd m2, m14
    add innerq, 1
    cmp innerq, fltsizeq
    jl .innerloop
    vphaddd m5, m9, m10
    vphaddd m6, m11, m12
%else
    vphaddd m5, m5, m6
    vphaddd m6, m7, m8
%endif
    vpsrad  m5, 7
    vpsrad  m6, 7
    vpackssdw m5, m5, m6
    vpermd m5, m15, m5
    vmovdqu [dstq + countq * 2], m5
    add fltposq, 0x40
    add countq, 0x10
    cmp countq, wq
    jl .loop
REP_RET
%endmacro

%if ARCH_X86_64
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
SCALE_FUNC 4
SCALE_FUNC X4
%endif
%endif
