;******************************************************************************
;* x86-optimized yuv2yuvX
;* Copyright 2020 Google LLC
;* Copyright (C) 2001-2011 Michael Niedermayer <michaelni@gmx.at>
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

;-----------------------------------------------------------------------------
; yuv2yuvX
;
; void ff_yuv2yuvX_<opt>(const int16_t *filter, int filterSize,
;                        int srcOffset, uint8_t *dest, int dstW,
;                        const uint8_t *dither, int offset);
;
;-----------------------------------------------------------------------------

%macro YUV2YUVX_FUNC 0
cglobal yuv2yuvX, 7, 7, 8, filter, filterSize, src, dest, dstW, dither, offset
%if notcpuflag(sse3)
%define movr mova
%define unroll 1
%else
%define movr movdqu
%define unroll 2
%endif
    movsxdifnidn         dstWq, dstWd
    movsxdifnidn         offsetq, offsetd
    movsxdifnidn         srcq, srcd
%if cpuflag(avx2)
    vpbroadcastq         m3, [ditherq]
%else
    movq                 xm3, [ditherq]
%endif ; avx2
    cmp                  offsetd, 0
    jz                   .offset

    ; offset != 0 path.
    psrlq                m5, m3, $18
    psllq                m3, m3, $28
    por                  m3, m3, m5

.offset:
    add offsetq, srcq
    movd                 xm1, filterSized
    SPLATW               m1, xm1, 0
    pxor                 m0, m0, m0
    mov                  filterSizeq, filterq
    mov                  srcq, [filterSizeq]
    punpcklbw            m3, m0
    psllw                m1, m1, 3
    paddw                m3, m3, m1
    psraw                m7, m3, 4
.outerloop:
    mova                 m4, m7
    mova                 m3, m7
%if cpuflag(sse3)
    mova                 m6, m7
    mova                 m1, m7
%endif
.loop:
%if cpuflag(avx2)
    vpbroadcastq         m0, [filterSizeq + 8]
%elif cpuflag(sse3)
    movddup              m0, [filterSizeq + 8]
%else
    mova                 m0, [filterSizeq + 8]
%endif
    pmulhw               m2, m0, [srcq + offsetq * 2]
    pmulhw               m5, m0, [srcq + offsetq * 2 + mmsize]
    paddw                m3, m3, m2
    paddw                m4, m4, m5
%if cpuflag(sse3)
    pmulhw               m2, m0, [srcq + offsetq * 2 + 2 * mmsize]
    pmulhw               m5, m0, [srcq + offsetq * 2 + 3 * mmsize]
    paddw                m6, m6, m2
    paddw                m1, m1, m5
%endif
    add                  filterSizeq, $10
    mov                  srcq, [filterSizeq]
    test                 srcq, srcq
    jnz                  .loop
    psraw                m3, m3, 3
    psraw                m4, m4, 3
%if cpuflag(sse3)
    psraw                m6, m6, 3
    psraw                m1, m1, 3
%endif
    packuswb             m3, m3, m4
%if cpuflag(sse3)
    packuswb             m6, m6, m1
%endif
    mov                  srcq, [filterq]
%if cpuflag(avx2)
    vpermq               m3, m3, 216
    vpermq               m6, m6, 216
%endif
    movr                 [destq + offsetq], m3
%if cpuflag(sse3)
    movr                 [destq + offsetq + mmsize], m6
%endif
    add                  offsetq, mmsize * unroll
    mov                  filterSizeq, filterq
    cmp                  offsetq, dstWq
    jb                  .outerloop
    REP_RET
%endmacro

INIT_MMX mmx
YUV2YUVX_FUNC
INIT_MMX mmxext
YUV2YUVX_FUNC
INIT_XMM sse3
YUV2YUVX_FUNC
%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
YUV2YUVX_FUNC
%endif
