;*****************************************************************************
;* x86-optimized functions for eq filter
;*
;* Original MPlayer filters by Richard Felker.
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License as published by
;* the Free Software Foundation; either version 2 of the License, or
;* (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;* GNU General Public License for more details.
;*
;* You should have received a copy of the GNU General Public License along
;* with FFmpeg; if not, write to the Free Software Foundation, Inc.,
;* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
;*****************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION .text

INIT_XMM sse2
cglobal process_one_line, 5, 7, 5, src, dst, contrast, brightness, w
    movd m3, contrastd
    movd m4, brightnessd
    movsx r5d, contrastw
    movsx r6d, brightnessw
    SPLATW m3, m3, 0
    SPLATW m4, m4, 0

    DEFINE_ARGS src, dst, tmp, scalar, w
    xor tmpd, tmpd
    pxor m0, m0
    pxor m1, m1
    mov scalard, wd
    and scalard, mmsize-1
    sar wd, 4
    cmp wd, 1
    jl .loop1

    .loop0:
        movu m1, [srcq]
        mova m2, m1
        punpcklbw m1, m0
        punpckhbw m2, m0
        psllw m1, 4
        psllw m2, 4
        pmulhw m1, m3
        pmulhw m2, m3
        paddw m1, m4
        paddw m2, m4
        packuswb m1, m2
        movu [dstq], m1
        add srcq, mmsize
        add dstq, mmsize
        sub wd, 1
        cmp wd, 0
        jne .loop0

    .loop1:
        cmp scalard, 0
        je .end
        movzx tmpd, byte [srcq]
        imul tmpd, r5d
        sar tmpd, 12
        add tmpd, r6d
        movd m1, tmpd
        packuswb m1, m0
        movd tmpd, m1
        mov [dstq], tmpb
        inc srcq
        inc dstq
        dec scalard
        jmp .loop1

    .end:
        RET
