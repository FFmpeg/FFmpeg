;*****************************************************************************
;* Copyright 2016 Anton Khirnov
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

INIT_XMM sse4
cglobal image_copy_plane_uc_from, 6, 7, 4, dst, dst_linesize, src, src_linesize, bw, height, rowpos
    add dstq, bwq
    add srcq, bwq
    neg bwq

.row_start:
    mov rowposq, bwq

.loop:
    movntdqa m0, [srcq + rowposq + 0 * mmsize]
    movntdqa m1, [srcq + rowposq + 1 * mmsize]
    movntdqa m2, [srcq + rowposq + 2 * mmsize]
    movntdqa m3, [srcq + rowposq + 3 * mmsize]

    mova [dstq + rowposq + 0 * mmsize], m0
    mova [dstq + rowposq + 1 * mmsize], m1
    mova [dstq + rowposq + 2 * mmsize], m2
    mova [dstq + rowposq + 3 * mmsize], m3

    add rowposq, 4 * mmsize
    jnz .loop

    add srcq, src_linesizeq
    add dstq, dst_linesizeq
    dec heightd
    jnz .row_start

    RET
