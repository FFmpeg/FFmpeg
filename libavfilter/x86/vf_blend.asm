;*****************************************************************************
;* x86-optimized functions for blend filter
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

%if ARCH_X86_64
SECTION_RODATA

pw_128: times 8 dw 128
pw_255: times 8 dw 255
pb_127: times 16 db 127
pb_128: times 16 db 128
pb_255: times 16 db 255

SECTION .text

INIT_XMM sse2
cglobal blend_xor, 9, 11, 2, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movu            m0, [topq + x]
        movu            m1, [bottomq + x]
        pxor            m0, m1
        mova    [dstq + x], m0
        add           r10q, mmsize
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_or, 9, 11, 2, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movu            m0, [topq + x]
        movu            m1, [bottomq + x]
        por             m0, m1
        mova    [dstq + x], m0
        add           r10q, mmsize
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_and, 9, 11, 2, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movu            m0, [topq + x]
        movu            m1, [bottomq + x]
        pand            m0, m1
        mova    [dstq + x], m0
        add           r10q, mmsize
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_addition, 9, 11, 2, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movu            m0, [topq + x]
        movu            m1, [bottomq + x]
        paddusb         m0, m1
        mova    [dstq + x], m0
        add           r10q, mmsize
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_subtract, 9, 11, 2, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movu            m0, [topq + x]
        movu            m1, [bottomq + x]
        psubusb         m0, m1
        mova    [dstq + x], m0
        add           r10q, mmsize
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_difference128, 9, 11, 4, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    pxor       m2, m2
    mova       m3, [pw_128]
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movh            m0, [topq + x]
        movh            m1, [bottomq + x]
        punpcklbw       m0, m2
        punpcklbw       m1, m2
        paddw           m0, m3
        psubw           m0, m1
        packuswb        m0, m0
        movh    [dstq + x], m0
        add           r10q, mmsize / 2
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_average, 9, 11, 3, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    pxor       m2, m2
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movh            m0, [topq + x]
        movh            m1, [bottomq + x]
        punpcklbw       m0, m2
        punpcklbw       m1, m2
        paddw           m0, m1
        psrlw           m0, 1
        packuswb        m0, m0
        movh    [dstq + x], m0
        add           r10q, mmsize / 2
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_addition128, 9, 11, 4, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    pxor       m2, m2
    mova       m3, [pw_128]
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movh            m0, [topq + x]
        movh            m1, [bottomq + x]
        punpcklbw       m0, m2
        punpcklbw       m1, m2
        paddw           m0, m1
        psubw           m0, m3
        packuswb        m0, m0
        movh    [dstq + x], m0
        add           r10q, mmsize / 2
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_darken, 9, 11, 2, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movu            m0, [topq + x]
        movu            m1, [bottomq + x]
        pminub          m0, m1
        mova    [dstq + x], m0
        add           r10q, mmsize
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_hardmix, 9, 11, 5, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    mova       m2, [pb_255]
    mova       m3, [pb_128]
    mova       m4, [pb_127]
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movu            m0, [topq + x]
        movu            m1, [bottomq + x]
        pxor            m1, m4
        pxor            m0, m3
        pcmpgtb         m1, m0
        pxor            m1, m2
        mova    [dstq + x], m1
        add           r10q, mmsize
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_lighten, 9, 11, 2, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movu            m0, [topq + x]
        movu            m1, [bottomq + x]
        pmaxub          m0, m1
        mova    [dstq + x], m0
        add           r10q, mmsize
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_phoenix, 9, 11, 4, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    mova       m3, [pb_255]
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movu            m0, [topq + x]
        movu            m1, [bottomq + x]
        mova            m2, m0
        pminub          m0, m1
        pmaxub          m1, m2
        mova            m2, m3
        psubusb         m2, m1
        paddusb         m2, m0
        mova    [dstq + x], m2
        add           r10q, mmsize
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

INIT_XMM ssse3
cglobal blend_difference, 9, 11, 3, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    pxor       m2, m2
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movh            m0, [topq + x]
        movh            m1, [bottomq + x]
        punpcklbw       m0, m2
        punpcklbw       m1, m2
        psubw           m0, m1
        pabsw           m0, m0
        packuswb        m0, m0
        movh    [dstq + x], m0
        add           r10q, mmsize / 2
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

cglobal blend_negation, 9, 11, 5, 0, top, top_linesize, bottom, bottom_linesize, dst, dst_linesize, width, start, end
    add      topq, widthq
    add   bottomq, widthq
    add      dstq, widthq
    sub      endq, startq
    pxor       m2, m2
    mova       m4, [pw_255]
    neg    widthq
.nextrow:
    mov       r10q, widthq
    %define      x  r10q

    .loop:
        movh            m0, [topq + x]
        movh            m1, [bottomq + x]
        punpcklbw       m0, m2
        punpcklbw       m1, m2
        mova            m3, m4
        psubw           m3, m0
        psubw           m3, m1
        pabsw           m3, m3
        mova            m0, m4
        psubw           m0, m3
        packuswb        m0, m0
        movh    [dstq + x], m0
        add           r10q, mmsize / 2
    jl .loop

    add          topq, top_linesizeq
    add       bottomq, bottom_linesizeq
    add          dstq, dst_linesizeq
    sub          endd, 1
    jg .nextrow
REP_RET

%endif
