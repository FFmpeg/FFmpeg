;******************************************************************************
;* TAK DSP SIMD optimizations
;*
;* Copyright (C) 2015 Paul B Mahol
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

SECTION_RODATA

pd_128: times 4 dd 128

SECTION .text

INIT_XMM sse2
cglobal tak_decorrelate_ls, 3, 3, 2, p1, p2, length
    shl                     lengthd, 2
    add                         p1q, lengthq
    add                         p2q, lengthq
    neg                     lengthq
.loop:
    mova                         m0, [p1q+lengthq+mmsize*0]
    mova                         m1, [p1q+lengthq+mmsize*1]
    paddd                        m0, [p2q+lengthq+mmsize*0]
    paddd                        m1, [p2q+lengthq+mmsize*1]
    mova     [p2q+lengthq+mmsize*0], m0
    mova     [p2q+lengthq+mmsize*1], m1
    add                     lengthq, mmsize*2
    jl .loop
    REP_RET

cglobal tak_decorrelate_sr, 3, 3, 2, p1, p2, length
    shl                     lengthd, 2
    add                         p1q, lengthq
    add                         p2q, lengthq
    neg                     lengthq

.loop:
    mova                         m0, [p2q+lengthq+mmsize*0]
    mova                         m1, [p2q+lengthq+mmsize*1]
    psubd                        m0, [p1q+lengthq+mmsize*0]
    psubd                        m1, [p1q+lengthq+mmsize*1]
    mova     [p1q+lengthq+mmsize*0], m0
    mova     [p1q+lengthq+mmsize*1], m1
    add                     lengthq, mmsize*2
    jl .loop
    REP_RET

cglobal tak_decorrelate_sm, 3, 3, 6, p1, p2, length
    shl                     lengthd, 2
    add                         p1q, lengthq
    add                         p2q, lengthq
    neg                     lengthq

.loop:
    mova                         m0, [p1q+lengthq]
    mova                         m1, [p2q+lengthq]
    mova                         m3, [p1q+lengthq+mmsize]
    mova                         m4, [p2q+lengthq+mmsize]
    mova                         m2, m1
    mova                         m5, m4
    psrad                        m2, 1
    psrad                        m5, 1
    psubd                        m0, m2
    psubd                        m3, m5
    paddd                        m1, m0
    paddd                        m4, m3
    mova              [p1q+lengthq], m0
    mova              [p2q+lengthq], m1
    mova       [p1q+lengthq+mmsize], m3
    mova       [p2q+lengthq+mmsize], m4
    add                     lengthq, mmsize*2
    jl .loop
    REP_RET

INIT_XMM sse4
cglobal tak_decorrelate_sf, 3, 3, 5, p1, p2, length, dshift, dfactor
    shl             lengthd, 2
    add                 p1q, lengthq
    add                 p2q, lengthq
    neg             lengthq

    movd                 m2, dshiftm
    movd                 m3, dfactorm
    pshufd               m3, m3, 0
    mova                 m4, [pd_128]

.loop:
    mova                 m0, [p1q+lengthq]
    mova                 m1, [p2q+lengthq]
    psrad                m1, m2
    pmulld               m1, m3
    paddd                m1, m4
    psrad                m1, 8
    pslld                m1, m2
    psubd                m1, m0
    mova      [p1q+lengthq], m1
    add             lengthq, mmsize
    jl .loop
    REP_RET
