;*****************************************************************************
;* x86-optimized functions for stereo3d filter
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
;*****************************************************************************

%include "libavutil/x86/x86util.asm"

%if ARCH_X86_64

SECTION_RODATA

; rgbrgbrgbrgb
; rrrrggggbbbb

shuf: db 0, 4, 8, 1,5, 9, 2, 6,10,3, 7,11,-1,-1,-1,-1
ex_r: db 0,-1,-1,-1,3,-1,-1,-1,6,-1,-1,-1, 9,-1,-1,-1
ex_g: db 1,-1,-1,-1,4,-1,-1,-1,7,-1,-1,-1,10,-1,-1,-1
ex_b: db 2,-1,-1,-1,5,-1,-1,-1,8,-1,-1,-1,11,-1,-1,-1

SECTION .text

INIT_XMM sse4
cglobal anaglyph, 11, 13, 16, 2*6*mmsize, dst, lsrc, rsrc, dst_linesize, l_linesize, r_linesize, width, height, ana_matrix_r, ana_matrix_g, ana_matrix_b
    movu                 m13, [ana_matrix_rq+ 0]
    movq                 m15, [ana_matrix_rq+16]
    pshufd               m10, m13, q0000
    pshufd               m11, m13, q1111
    pshufd               m12, m13, q2222
    pshufd               m13, m13, q3333
    pshufd               m14, m15, q0000
    pshufd               m15, m15, q1111
    mova      [rsp+mmsize*0], m10
    mova      [rsp+mmsize*1], m11
    mova      [rsp+mmsize*2], m12
    mova      [rsp+mmsize*3], m13
    mova      [rsp+mmsize*4], m14
    mova      [rsp+mmsize*5], m15

    movu                 m13, [ana_matrix_gq+ 0]
    movq                 m15, [ana_matrix_gq+16]
    pshufd               m10, m13, q0000
    pshufd               m11, m13, q1111
    pshufd               m12, m13, q2222
    pshufd               m13, m13, q3333
    pshufd               m14, m15, q0000
    pshufd               m15, m15, q1111
    mova     [rsp+mmsize*6 ], m10
    mova     [rsp+mmsize*7 ], m11
    mova     [rsp+mmsize*8 ], m12
    mova     [rsp+mmsize*9 ], m13
    mova     [rsp+mmsize*10], m14
    mova     [rsp+mmsize*11], m15

    movu                 m13, [ana_matrix_bq+ 0]
    movq                 m15, [ana_matrix_bq+16]
    pshufd               m10, m13, q0000
    pshufd               m11, m13, q1111
    pshufd               m12, m13, q2222
    pshufd               m13, m13, q3333
    pshufd               m14, m15, q0000
    pshufd               m15, m15, q1111
.nextrow:
    mov       r11q, widthq
    mov       r12q, 0
    %define      o  r12q

    .loop:
        movu                 m0, [lsrcq+o+0]
        pshufb               m1, m0, [ex_r]
        pshufb               m2, m0, [ex_g]
        pshufb               m3, m0, [ex_b]
        movu                 m0, [rsrcq+o+0]
        pshufb               m4, m0, [ex_r]
        pshufb               m5, m0, [ex_g]
        pshufb               m6, m0, [ex_b]
        pmulld               m1, [rsp+mmsize*0]
        pmulld               m2, [rsp+mmsize*1]
        pmulld               m3, [rsp+mmsize*2]
        pmulld               m4, [rsp+mmsize*3]
        pmulld               m5, [rsp+mmsize*4]
        pmulld               m6, [rsp+mmsize*5]
        paddd                m1, m2
        paddd                m3, m4
        paddd                m5, m6
        paddd                m1, m3
        paddd                m1, m5

        movu                 m0, [lsrcq+o+0]
        pshufb               m7, m0, [ex_r]
        pshufb               m2, m0, [ex_g]
        pshufb               m3, m0, [ex_b]
        movu                 m0, [rsrcq+o+0]
        pshufb               m4, m0, [ex_r]
        pshufb               m5, m0, [ex_g]
        pshufb               m6, m0, [ex_b]
        pmulld               m7, [rsp+mmsize*6]
        pmulld               m2, [rsp+mmsize*7]
        pmulld               m3, [rsp+mmsize*8]
        pmulld               m4, [rsp+mmsize*9]
        pmulld               m5, [rsp+mmsize*10]
        pmulld               m6, [rsp+mmsize*11]
        paddd                m7, m2
        paddd                m3, m4
        paddd                m5, m6
        paddd                m7, m3
        paddd                m7, m5

        movu                 m0, [lsrcq+o+0]
        pshufb               m8, m0, [ex_r]
        pshufb               m2, m0, [ex_g]
        pshufb               m3, m0, [ex_b]
        movu                 m0, [rsrcq+o+0]
        pshufb               m4, m0, [ex_r]
        pshufb               m5, m0, [ex_g]
        pshufb               m6, m0, [ex_b]
        pmulld               m8, m10
        pmulld               m2, m11
        pmulld               m3, m12
        pmulld               m4, m13
        pmulld               m5, m14
        pmulld               m6, m15
        paddd                m8, m2
        paddd                m3, m4
        paddd                m5, m6
        paddd                m8, m3
        paddd                m8, m5

        psrld                m1, 16
        psrld                m7, 16
        psrld                m8, 16

        packusdw             m1, m7
        packusdw             m8, m8
        packuswb             m1, m8
        pshufb               m1, [shuf]

        movq         [dstq+o+0], m1
        psrldq               m1, 8
        movd         [dstq+o+8], m1
        add                r12d, 12
        sub                r11d, 4
    jg .loop

    add          dstq, dst_linesizeq
    add         lsrcq, l_linesizeq
    add         rsrcq, r_linesizeq
    sub       heightd, 1
    jg .nextrow
REP_RET
%endif
