;******************************************************************************
;* V210 SIMD unpack
;* Copyright (c) 2011 Loren Merritt <lorenm@u.washington.edu>
;* Copyright (c) 2011 Kieran Kunhya <kieran@kunhya.com>
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

v210_mask: times 4 dd 0x3ff
v210_mult: dw 64,4,64,4,64,4,64,4
v210_luma_shuf: db 8,9,0,1,2,3,12,13,4,5,6,7,-1,-1,-1,-1
v210_chroma_shuf: db 0,1,8,9,6,7,-1,-1,2,3,4,5,12,13,-1,-1

SECTION .text

%macro v210_planar_unpack 2

; v210_planar_unpack(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int width)
cglobal v210_planar_unpack_%1_%2, 5, 5, 7
    movsxdifnidn r4, r4d
    lea    r1, [r1+2*r4]
    add    r2, r4
    add    r3, r4
    neg    r4

    mova   m3, [v210_mult]
    mova   m4, [v210_mask]
    mova   m5, [v210_luma_shuf]
    mova   m6, [v210_chroma_shuf]
.loop
%ifidn %1, unaligned
    movu   m0, [r0]
%else
    mova   m0, [r0]
%endif

    pmullw m1, m0, m3
    psrld  m0, 10
    psrlw  m1, 6  ; u0 v0 y1 y2 v1 u2 y4 y5
    pand   m0, m4 ; y0 __ u1 __ y3 __ v2 __

    shufps m2, m1, m0, 0x8d ; y1 y2 y4 y5 y0 __ y3 __
    pshufb m2, m5 ; y0 y1 y2 y3 y4 y5 __ __
    movu   [r1+2*r4], m2

    shufps m1, m0, 0xd8 ; u0 v0 v1 u2 u1 __ v2 __
    pshufb m1, m6 ; u0 u1 u2 __ v0 v1 v2 __
    movq   [r2+r4], m1
    movhps [r3+r4], m1

    add r0, mmsize
    add r4, 6
    jl  .loop

    REP_RET
%endmacro

INIT_XMM
v210_planar_unpack unaligned, ssse3
%if HAVE_AVX_EXTERNAL
INIT_AVX
v210_planar_unpack unaligned, avx
%endif

INIT_XMM
v210_planar_unpack aligned, ssse3
%if HAVE_AVX_EXTERNAL
INIT_AVX
v210_planar_unpack aligned, avx
%endif
