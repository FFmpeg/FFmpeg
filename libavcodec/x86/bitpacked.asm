;******************************************************************************
;* Bitpacked 10-bit YUV 4:2:2 unpacking
;* Copyright (c) 2026 Devin Heitmueller <dheitmueller@ltnglobal.com>
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

unpack_shuf_avx512icl:
    db  1, 0,  2, 1,  3, 2,  4, 3,  6, 5,  7, 6,  8, 7,  9, 8
    db 11,10, 12,11, 13,12, 14,13, 16,15, 17,16, 18,17, 19,18
    db 21,20, 22,21, 23,22, 24,23, 26,25, 27,26, 28,27, 29,28
    db 31,30, 32,31, 33,32, 34,33, 36,35, 37,36, 38,37, 39,38
y_shuf_avx512icl:
    db  2, 3,  6, 7, 10,11, 14,15, 18,19, 22,23, 26,27, 30,31
    db 34,35, 38,39, 42,43, 46,47, 50,51, 54,55, 58,59, 62,63
    times 32 db 0
u_shuf_avx512icl:
    db  0, 1,  8, 9, 16,17, 24,25, 32,33, 40,41, 48,49, 56,57
    times 48 db 0
v_shuf_avx512icl:
    db  4, 5, 12,13, 20,21, 28,29, 36,37, 44,45, 52,53, 60,61
    times 48 db 0
input_mask_avx512icl: dq 0x000000ffffffffff
tail_mask_avx512icl:  dq 0x00000000000fffff

ALIGN 32
unpack_shuf_avx2:
    db 1,0, 2,1, 3,2, 4,3, 6,5, 7,6, 8,7, 9,8
    db 5,4, 6,5, 7,6, 8,7, 10,9, 11,10, 12,11, 13,12
u_shuf_ymm:
    db 0,1, 8,9, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    db 0,1, 8,9, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
v_shuf_ymm:
    db 4,5, 12,13, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    db 4,5, 12,13, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1

unpack_shuf: db 1,0, 2,1, 3,2, 4,3, 6,5, 7,6, 8,7, 9,8
unpack_mult: dw 1,4,16,64, 1,4,16,64
y_shuf:      db 2,3, 6,7, 10,11, 14,15, -1,-1,-1,-1,-1,-1,-1,-1
u_shuf:      db 0,1, 8,9, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
v_shuf:      db 4,5, 12,13, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1

SECTION .text

; ff_bitpacked_unpack_yuv422p10(const uint8_t *src,
;                               uint16_t *y, uint16_t *u, uint16_t *v,
;                               int width)
; Form four big-endian 16-bit windows per five input bytes. Multiplication by
; 1, 4, 16 and 64 aligns all four samples for a common six-bit right shift.
INIT_XMM ssse3
cglobal bitpacked_unpack_yuv422p10, 5, 5, 8, src, y, u, v, w
    movsxdifnidn wq, wd
    lea            yq, [yq+2*wq]
    add            uq, wq
    add            vq, wq
    neg            wq

    mova           m4, [unpack_shuf]
    mova           m5, [unpack_mult]
    mova           m6, [y_shuf]
    mova           m7, [u_shuf]

.loop:
    movq           m0, [srcq]
    pinsrw         m0, [srcq+8], 4
    movq           m1, [srcq+10]
    pinsrw         m1, [srcq+18], 4

    pshufb         m0, m4
    pshufb         m1, m4
    pmullw         m0, m5
    pmullw         m1, m5
    psrlw          m0, 6
    psrlw          m1, 6

    mova           m2, m0
    mova           m3, m1
    pshufb         m2, m6
    pshufb         m3, m6
    punpcklqdq     m2, m3
    movu           [yq+2*wq], m2

    mova           m2, m0
    mova           m3, m1
    pshufb         m2, m7
    pshufb         m3, m7
    punpckldq      m2, m3
    movq           [uq+wq], m2

    pshufb         m0, [v_shuf]
    pshufb         m1, [v_shuf]
    punpckldq      m0, m1
    movq           [vq+wq], m0

    add            srcq, 20
    add            wq, 8
    jl             .loop

    RET

%if HAVE_AVX2_EXTERNAL
%macro unpack_16 0
    ; m0 contains the first and third 10-byte groups; m1 the second and fourth.
    movu           m0, [srcq]
    movq           xm1, [srcq+10]
    pinsrw         xm1, [srcq+18], 4
    movq           xm2, [srcq+30]
    pinsrw         xm2, [srcq+38], 4
    vinserti128    m1, m1, xm2, 1

    pshufb         m0, m4
    pshufb         m1, m5
    pmullw         m0, m6
    pmullw         m1, m6
    psrlw          m0, 6
    psrlw          m1, 6

    pshufb         m2, m0, m7
    pshufb         m3, m1, m7
    punpcklqdq     m2, m3
    movu           [yq], m2

    pshufb         m2, m0, [u_shuf_ymm]
    pshufb         m3, m1, [u_shuf_ymm]
    punpckldq      m2, m3
    vpermq         m2, m2, 0xd8
    movu           [uq], xm2

    pshufb         m2, m0, [v_shuf_ymm]
    pshufb         m3, m1, [v_shuf_ymm]
    punpckldq      m2, m3
    vpermq         m2, m2, 0xd8
    movu           [vq], xm2

    add            srcq, 40
    add            yq, 32
    add            uq, 16
    add            vq, 16
%endmacro

INIT_YMM avx2
cglobal bitpacked_unpack_yuv422p10, 5, 5, 8, src, y, u, v, w
    mova           m4, [unpack_shuf_avx2]
    VBROADCASTI128 m5, [unpack_shuf]
    VBROADCASTI128 m6, [unpack_mult]
    VBROADCASTI128 m7, [y_shuf]

    sub            wd, 16
    jl             .tail
.loop:
    unpack_16
    sub            wd, 16
    jge            .loop

.tail:
    add            wd, 16
    jz             .ret
    movq           xm0, [srcq]
    pinsrw         xm0, [srcq+8], 4
    movq           xm1, [srcq+10]
    pinsrw         xm1, [srcq+18], 4
    vinserti128    m0, m0, xm1, 1

    pshufb         m0, m5
    pmullw         m0, m6
    psrlw          m0, 6

    pshufb         m1, m0, m7
    vextracti128   xm2, m1, 1
    punpcklqdq     xm1, xm2
    movu           [yq], xm1

    VBROADCASTI128 m3, [u_shuf]
    pshufb         m1, m0, m3
    vextracti128   xm2, m1, 1
    punpckldq      xm1, xm2
    movq           [uq], xm1

    VBROADCASTI128 m3, [v_shuf]
    pshufb         m1, m0, m3
    vextracti128   xm2, m1, 1
    punpckldq      xm1, xm2
    movq           [vq], xm1

.ret:
    RET
%endif

%if HAVE_AVX512ICL_EXTERNAL
INIT_ZMM avx512icl
cglobal bitpacked_unpack_yuv422p10, 5, 5, 7, src, y, u, v, w
    mova           m2, [unpack_shuf_avx512icl]
    VBROADCASTI128 m3, [unpack_mult]
    mova           m4, [y_shuf_avx512icl]
    mova           m5, [u_shuf_avx512icl]
    mova           m6, [v_shuf_avx512icl]
    kmovq          k1, [input_mask_avx512icl]
    kmovq          k2, [tail_mask_avx512icl]

    test           wd, 8
    jz             .setup
    vmovdqu8       m0{k2}{z}, [srcq]
    vpermb         m0, m2, m0
    pmullw         m0, m3
    psrlw          m0, 6

    vpermb         m1, m4, m0
    movu           [yq], xm1
    vpermb         m1, m5, m0
    movq           [uq], xm1
    vpermb         m1, m6, m0
    movq           [vq], xm1

    add            srcq, 20
    add            yq, 16
    add            uq, 8
    add            vq, 8
    sub            wd, 8
    jz             .ret

.setup:
    movsxdifnidn   wq, wd
    lea            yq, [yq+2*wq]
    add            uq, wq
    add            vq, wq
    neg            wq

.loop:
    vmovdqu8       m0{k1}{z}, [srcq]
    vpermb         m0, m2, m0
    pmullw         m0, m3
    psrlw          m0, 6

    vpermb         m1, m4, m0
    movu           [yq+2*wq], ym1
    vpermb         m1, m5, m0
    movu           [uq+wq], xm1
    vpermb         m1, m6, m0
    movu           [vq+wq], xm1

    add            srcq, 40
    add            wq, 16
    jl             .loop

.ret:
    RET
%endif
