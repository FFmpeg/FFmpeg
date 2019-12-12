;*****************************************************************************
;* x86-optimized functions for transpose filter
;*
;* Copyright (C) 2019 Paul B Mahol
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
; void ff_transpose_8x8(uint8_t *src, ptrdiff_t src_linesize,
;                       uint8_t *dst, ptrdiff_t dst_linesize)
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal transpose_8x8_8, 4,5,8, src, src_linesize, dst, dst_linesize, linesize3
    lea     linesize3q, [src_linesizeq * 3]
    movq    m0, [srcq + src_linesizeq * 0]
    movq    m1, [srcq + src_linesizeq * 1]
    movq    m2, [srcq + src_linesizeq * 2]
    movq    m3, [srcq + linesize3q]
    lea   srcq, [srcq + src_linesizeq * 4]
    movq    m4, [srcq + src_linesizeq * 0]
    movq    m5, [srcq + src_linesizeq * 1]
    movq    m6, [srcq + src_linesizeq * 2]
    movq    m7, [srcq + linesize3q]

    TRANSPOSE_8X8B 0, 1, 2, 3, 4, 5, 6, 7

    lea                  linesize3q, [dst_linesizeq * 3]
    movq [dstq + dst_linesizeq * 0], m0
    movq [dstq + dst_linesizeq * 1], m1
    movq [dstq + dst_linesizeq * 2], m2
    movq [dstq + linesize3q], m3
    lea                        dstq, [dstq + dst_linesizeq * 4]
    movq [dstq + dst_linesizeq * 0], m4
    movq [dstq + dst_linesizeq * 1], m5
    movq [dstq + dst_linesizeq * 2], m6
    movq [dstq + linesize3q], m7
    RET

cglobal transpose_8x8_16, 4,5,9, ARCH_X86_32 * 32, src, src_linesize, dst, dst_linesize, linesize3
    lea     linesize3q, [src_linesizeq * 3]
    movu    m0, [srcq + src_linesizeq * 0]
    movu    m1, [srcq + src_linesizeq * 1]
    movu    m2, [srcq + src_linesizeq * 2]
    movu    m3, [srcq + linesize3q]
    lea   srcq, [srcq + src_linesizeq * 4]
    movu    m4, [srcq + src_linesizeq * 0]
    movu    m5, [srcq + src_linesizeq * 1]
    movu    m6, [srcq + src_linesizeq * 2]
    movu    m7, [srcq + linesize3q]

%if ARCH_X86_64
    TRANSPOSE8x8W 0, 1, 2, 3, 4, 5, 6, 7, 8
%else
    TRANSPOSE8x8W 0, 1, 2, 3, 4, 5, 6, 7, [rsp], [rsp + 16]
%endif

    lea                  linesize3q, [dst_linesizeq * 3]
    movu [dstq + dst_linesizeq * 0], m0
    movu [dstq + dst_linesizeq * 1], m1
    movu [dstq + dst_linesizeq * 2], m2
    movu [dstq + linesize3q], m3
    lea                        dstq, [dstq + dst_linesizeq * 4]
    movu [dstq + dst_linesizeq * 0], m4
    movu [dstq + dst_linesizeq * 1], m5
    movu [dstq + dst_linesizeq * 2], m6
    movu [dstq + linesize3q], m7
    RET
