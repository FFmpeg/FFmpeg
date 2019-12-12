;*****************************************************************************
;* x86-optimized functions for maskedclamp filter
;*
;* Copyright (c) 2019 Paul B Mahol
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

;------------------------------------------------------------------------------
; void ff_maskedclamp(const uint8_t *src, uint8_t *dst,
;                     const uint8_t *darksrc,
;                     const uint8_t *brightsrc,
;                     int w, int undershoot, int overshoot)
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal maskedclamp8, 5,5,5, src, dst, dark, bright, w, undershoot, overshoot
    movsxdifnidn wq, wd

    add        srcq, wq
    add       darkq, wq
    add     brightq, wq
    add        dstq, wq
    neg          wq

    movd         m3, r5m
    punpcklbw    m3, m3
    SPLATW       m3, m3

    movd         m4, r6m
    punpcklbw    m4, m4
    SPLATW       m4, m4

    .loop:
        movu                  m0, [srcq + wq]
        movu                  m1, [darkq + wq]
        movu                  m2, [brightq + wq]

        psubusb               m1, m3
        paddusb               m2, m4
        CLIPUB                m0, m1, m2
        mova         [dstq + wq], m0

        add                   wq, mmsize
        jl .loop
    RET

INIT_XMM sse4
cglobal maskedclamp16, 5,5,5, src, dst, dark, bright, w, undershoot, overshoot
    shl          wd, 1

    add        srcq, wq
    add       darkq, wq
    add     brightq, wq
    add        dstq, wq
    neg          wq

    movd         m3, r5m
    SPLATW       m3, m3

    movd         m4, r6m
    SPLATW       m4, m4

    .loop:
        movu                  m0, [srcq + wq]
        movu                  m1, [darkq + wq]
        movu                  m2, [brightq + wq]

        psubusw               m1, m3
        paddusw               m2, m4
        pmaxuw                m0, m1
        pminuw                m0, m2
        mova         [dstq + wq], m0

        add                   wq, mmsize
        jl .loop
    RET
