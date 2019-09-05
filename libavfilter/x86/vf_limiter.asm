;*****************************************************************************
;* x86-optimized functions for limiter filter
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

INIT_XMM sse2

cglobal limiter_8bit, 6, 7, 3, src, dst, slinesize, dlinesize, w, h, x
    movsxdifnidn wq, wd
    add        srcq, wq
    add        dstq, wq
    neg          wq
    movd         m1, r6m
    punpcklbw    m1, m1
    SPLATW       m1, m1
    movd         m2, r7m
    punpcklbw    m2, m2
    SPLATW       m2, m2
.nextrow:
    mov          xq, wq

    .loop:
        movu           m0, [srcq + xq]
        CLIPUB         m0, m1, m2
        mova    [dstq+xq], m0
        add            xq, mmsize
    jl .loop

    add        srcq, slinesizeq
    add        dstq, dlinesizeq
    sub        hd, 1
    jg .nextrow
    RET

INIT_XMM sse4

cglobal limiter_16bit, 6, 7, 3, src, dst, slinesize, dlinesize, w, h, x
    shl          wd, 1
    add        srcq, wq
    add        dstq, wq
    neg          wq
    movd         m1, r6m
    SPLATW       m1, m1
    movd         m2, r7m
    SPLATW       m2, m2
.nextrow:
    mov          xq, wq

    .loop:
        movu           m0, [srcq + xq]
        pmaxuw         m0, m1
        pminuw         m0, m2
        mova    [dstq+xq], m0
        add            xq, mmsize
    jl .loop

    add        srcq, slinesizeq
    add        dstq, dlinesizeq
    sub        hd, 1
    jg .nextrow
    RET
