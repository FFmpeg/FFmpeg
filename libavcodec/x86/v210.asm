;******************************************************************************
;* V210 SIMD unpack
;* Copyright (c) 2011 Loren Merritt <lorenm@u.washington.edu>
;* Copyright (c) 2011 Kieran Kunhya <kieran@kunhya.com>
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

SECTION_RODATA 64

perm_y:
    db  0,1,   4,5,   6,7,   8,9,  12,13, 14,15, 16,17, 20,21
    db 22,23, 24,25, 28,29, 30,31, 32,33, 36,37, 38,39, 40,41
    db 44,45, 46,47, 48,49, 52,53, 54,55, 56,57, 60,61, 62,63
times 16 db 0xff ; align to 64

perm_uv:
    db  0,1,   4,5,  10,11, 16,17, 20,21, 26,27, 32,33, 36,37
    db 42,43, 48,49, 52,53, 58,59
times 8 db 0xff ; align to 32
    db  2,3,   8,9,  12,13, 18,19, 24,25, 28,29, 34,35, 40,41
    db 44,45, 50,51, 56,57, 60,61
times 8 db 0xff ; align to 32

; for AVX2 version only
v210_luma_permute: dd 0,1,2,4,5,6,7,7  ; 32-byte alignment required
v210_chroma_shuf2: db 0,1,2,3,4,5,8,9,10,11,12,13,-1,-1,-1,-1
v210_luma_shuf_avx2: db 0,1,4,5,6,7,8,9,12,13,14,15,-1,-1,-1,-1
v210_chroma_shuf_avx2: db 0,1,4,5,10,11,-1,-1,2,3,8,9,12,13,-1,-1

v210_mult: dw 64,4,64,4,64,4,64,4
v210_luma_shuf: db 8,9,0,1,2,3,12,13,4,5,6,7,-1,-1,-1,-1
v210_chroma_shuf: db 0,1,8,9,6,7,-1,-1,2,3,4,5,12,13,-1,-1

shift: times 4 dw 6, 2
kmask: dw 0x5555, 0xaaaa

SECTION .text

%macro v210_planar_unpack 1

; v210_planar_unpack(const uint32_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int width)
cglobal v210_planar_unpack_%1, 5, 5, 6 + 2 * cpuflag(avx2), src, y, u, v, w
    movsxdifnidn wq, wd
    lea    yq, [yq+2*wq]
    add    uq, wq
    add    vq, wq
    neg    wq

    VBROADCASTI128   m3, [v210_mult]

%if cpuflag(avx2)
    VBROADCASTI128   m4, [v210_luma_shuf_avx2]
    VBROADCASTI128   m5, [v210_chroma_shuf_avx2]
    mova             m6, [v210_luma_permute]
    VBROADCASTI128   m7, [v210_chroma_shuf2]
%else
    VBROADCASTI128   m4, [v210_luma_shuf]
    VBROADCASTI128   m5, [v210_chroma_shuf]
%endif

.loop:
%ifidn %1, unaligned
    movu   m0, [srcq]  ; yB v5 yA  u5 y9 v4  y8 u4 y7  v3 y6 u3  y5 v2 y4  u2 y3 v1  y2 u1 y1  v0 y0 u0
%else
    mova   m0, [srcq]
%endif

    pmullw m1, m0, m3 ; shifts the 1st and 3rd sample of each dword into the high 10 bits of each word
    pslld  m0, 12     ; shifts the 2nd sample of each dword into the high 10 bits of each dword
    psrlw  m1, 6      ; shifts the 1st and 3rd samples back into the low 10 bits
    psrld  m0, 22     ; shifts the 2nd sample back into the low 10 bits of each dword

%if cpuflag(avx2)
    vpblendd m2, m1, m0, 0x55 ; merge the odd dwords from m0 and even from m1 ; yB yA 00 y9 y8 y7 00 y6 y5 y4 00 y3 y2 y1 00 y0
    pshufb m2, m4                      ; 00 00 yB yA y9 y8 y7 y6 00 00 y5 y4 y3 y2 y1 y0
    vpermd m2, m6, m2                  ; 00 00 00 00 yB yA y9 y8 y7 y6 y5 y4 y3 y2 y1 y0
    movu   [yq+2*wq], m2

    vpblendd m1, m1, m0, 0xaa ; merge the even dwords from m0 and odd from m1 ; 00 v5 u5 v4 00 u4 v3 u3 00 v2 u2 v1 00 u1 v0 u0
    pshufb m1, m5                      ; 00 v5 v4 v3 00 u5 u4 u3 00 v2 v1 v0 00 u2 u1 u0
    vpermq m1, m1, 0xd8                ; 00 v5 v4 v3 00 v2 v1 v0 00 u5 u4 u3 00 u2 u1 u0
    pshufb m1, m7                      ; 00 00 v5 v4 v3 v2 v1 v0 00 00 u5 u4 u3 u2 u1 u0

    movu   [uq+wq], xm1
    vextracti128 [vq+wq], m1, 1
%else
    shufps m2, m1, m0, 0x8d            ; 00 y9 00 y6 yB yA y8 y7 00 y3 00 y0 y5 y4 y2 y1
    pshufb m2, m4                      ; 00 00 yB yA y9 y8 y7 y6 00 00 y5 y4 y3 y2 y1 y0
    movu   [yq+2*wq], m2

    shufps m1, m0, 0xd8                ; 00 v5 00 u4 u5 v4 v3 u3 00 v2 00 u1 u2 v1 v0 u0
    pshufb m1, m5                      ; 00 v5 v4 v3 00 u5 u4 u3 00 v2 v1 v0 00 u2 u1 u0

    movq   [uq+wq], m1
    movhps [vq+wq], m1
%endif

    add srcq, mmsize
    add wq, (mmsize*3)/8
    jl  .loop

    RET
%endmacro

INIT_XMM ssse3
v210_planar_unpack unaligned

%if HAVE_AVX_EXTERNAL
INIT_XMM avx
v210_planar_unpack unaligned
%endif

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
v210_planar_unpack unaligned
%endif

INIT_XMM ssse3
v210_planar_unpack aligned

%if HAVE_AVX_EXTERNAL
INIT_XMM avx
v210_planar_unpack aligned
%endif

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
v210_planar_unpack aligned
%endif

%if HAVE_AVX512ICL_EXTERNAL

INIT_ZMM avx512icl

cglobal v210_planar_unpack, 5, 5, 6, src, y, u, v, w
    movsxdifnidn wq, wd
    lea    yq, [yq+2*wq]
    add    uq, wq
    add    vq, wq
    neg    wq

    kmovw k1, [kmask]   ; odd dword mask
    kmovw k2, [kmask+2] ; even dword mask

    VBROADCASTI128 m0, [shift]
    mova           m1, [perm_y]
    mova           m2, [perm_uv]

    .loop:
        movu    m3, [srcq]
        vpsllvw m4, m3, m0
        pslld   m5, m3, 12
        psrlw   m4, 6
        psrld   m5, 22

        vpblendmd m3{k1}, m4, m5
        vpermb    m3, m1, m3 ; could use vpcompressw
        movu      [yq+2*wq], m3

        vpblendmd     m5{k2}, m4, m5
        vpermb        m5, m2, m5
        movu          [uq+wq], ym5
        vextracti32x8 [vq+wq], zm5, 1

        add srcq, mmsize
        add wq, (mmsize*3)/8
    jl  .loop
RET

%endif
