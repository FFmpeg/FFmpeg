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

SECTION_RODATA

; rgbrgbrgbrgb
; rrrrggggbbbb

shuf: db 0, 4, 8, 1,5, 9, 2, 6,10,3, 7,11,-1,-1,-1,-1
ex_r: db 0,-1,-1,-1,3,-1,-1,-1,6,-1,-1,-1, 9,-1,-1,-1
ex_g: db 1,-1,-1,-1,4,-1,-1,-1,7,-1,-1,-1,10,-1,-1,-1
ex_b: db 2,-1,-1,-1,5,-1,-1,-1,8,-1,-1,-1,11,-1,-1,-1

SECTION .text

INIT_XMM sse4
%if ARCH_X86_64
cglobal anaglyph, 6, 10, 14, 2*6*mmsize, dst, lsrc, rsrc, dst_linesize, l_linesize, r_linesize, width, height, o, cnt
%define ana_matrix_rq r6q
%define ana_matrix_gq r7q
%define ana_matrix_bq r8q

%else ; ARCH_X86_32
%if HAVE_ALIGNED_STACK
cglobal anaglyph, 3, 7, 8, 2*9*mmsize, dst, lsrc, rsrc, dst_linesize, l_linesize, o, cnt
%else
cglobal anaglyph, 3, 6, 8, 2*9*mmsize, dst, lsrc, rsrc, dst_linesize, o, cnt
%define l_linesizeq r4mp
%endif ; HAVE_ALIGNED_STACK
%define ana_matrix_rq r3q
%define ana_matrix_gq r4q
%define ana_matrix_bq r5q
%define r_linesizeq r5mp
%define widthd  r6mp
%define heightd r7mp
%define  m8 [rsp+mmsize*12]
%define  m9 [rsp+mmsize*13]
%define m10 [rsp+mmsize*14]
%define m11 [rsp+mmsize*15]
%define m12 [rsp+mmsize*16]
%define m13 [rsp+mmsize*17]
%endif ; ARCH

    mov        ana_matrix_rq, r8m
    mov        ana_matrix_gq, r9m
    mov        ana_matrix_bq, r10m
    movu                  m3, [ana_matrix_rq+ 0]
    movq                  m5, [ana_matrix_rq+16]
    pshufd                m0, m3, q0000
    pshufd                m1, m3, q1111
    pshufd                m2, m3, q2222
    pshufd                m3, m3, q3333
    pshufd                m4, m5, q0000
    pshufd                m5, m5, q1111
    mova      [rsp+mmsize*0], m0
    mova      [rsp+mmsize*1], m1
    mova      [rsp+mmsize*2], m2
    mova      [rsp+mmsize*3], m3
    mova      [rsp+mmsize*4], m4
    mova      [rsp+mmsize*5], m5

    movu                  m3, [ana_matrix_gq+ 0]
    movq                  m5, [ana_matrix_gq+16]
    pshufd                m0, m3, q0000
    pshufd                m1, m3, q1111
    pshufd                m2, m3, q2222
    pshufd                m3, m3, q3333
    pshufd                m4, m5, q0000
    pshufd                m5, m5, q1111
    mova     [rsp+mmsize*6 ], m0
    mova     [rsp+mmsize*7 ], m1
    mova     [rsp+mmsize*8 ], m2
    mova     [rsp+mmsize*9 ], m3
    mova     [rsp+mmsize*10], m4
    mova     [rsp+mmsize*11], m5

%if ARCH_X86_64
    movu                 m11, [ana_matrix_bq+ 0]
    movq                 m13, [ana_matrix_bq+16]
    pshufd                m8, m11, q0000
    pshufd                m9, m11, q1111
    pshufd               m10, m11, q2222
    pshufd               m11, m11, q3333
    pshufd               m12, m13, q0000
    pshufd               m13, m13, q1111
    mov               widthd, dword widthm
    mov              heightd, dword heightm
%else
    movu                  m3, [ana_matrix_bq+ 0]
    movq                  m5, [ana_matrix_bq+16]
    pshufd                m0, m3, q0000
    pshufd                m1, m3, q1111
    pshufd                m2, m3, q2222
    pshufd                m3, m3, q3333
    pshufd                m4, m5, q0000
    pshufd                m5, m5, q1111
    mova     [rsp+mmsize*12], m0
    mova     [rsp+mmsize*13], m1
    mova     [rsp+mmsize*14], m2
    mova     [rsp+mmsize*15], m3
    mova     [rsp+mmsize*16], m4
    mova     [rsp+mmsize*17], m5
    mov        dst_linesizeq, r3m
%if HAVE_ALIGNED_STACK
    mov          l_linesizeq, r4m
%endif
%endif ; ARCH

.nextrow:
    mov                   od, widthd
    xor                 cntd, cntd

    .loop:
        movu                 m3, [lsrcq+cntq]
        pshufb               m1, m3, [ex_r]
        pshufb               m2, m3, [ex_g]
        pshufb               m3, [ex_b]
        movu                 m0, [rsrcq+cntq]
        pshufb               m4, m0, [ex_r]
        pshufb               m5, m0, [ex_g]
        pshufb               m0, [ex_b]
        pmulld               m1, [rsp+mmsize*0]
        pmulld               m2, [rsp+mmsize*1]
        pmulld               m3, [rsp+mmsize*2]
        pmulld               m4, [rsp+mmsize*3]
        pmulld               m5, [rsp+mmsize*4]
        pmulld               m0, [rsp+mmsize*5]
        paddd                m1, m2
        paddd                m3, m4
        paddd                m5, m0
        paddd                m1, m3
        paddd                m1, m5

        movu                 m3, [lsrcq+cntq]
        pshufb               m7, m3, [ex_r]
        pshufb               m2, m3, [ex_g]
        pshufb               m3, [ex_b]
        movu                 m0, [rsrcq+cntq]
        pshufb               m4, m0, [ex_r]
        pshufb               m5, m0, [ex_g]
        pshufb               m0, [ex_b]
        pmulld               m7, [rsp+mmsize*6]
        pmulld               m2, [rsp+mmsize*7]
        pmulld               m3, [rsp+mmsize*8]
        pmulld               m4, [rsp+mmsize*9]
        pmulld               m5, [rsp+mmsize*10]
        pmulld               m0, [rsp+mmsize*11]
        paddd                m7, m2
        paddd                m3, m4
        paddd                m5, m0
        paddd                m7, m3
        paddd                m7, m5

        movu                 m4, [lsrcq+cntq]
        pshufb               m2, m4, [ex_r]
        pshufb               m3, m4, [ex_g]
        pshufb               m4, [ex_b]
        movu                 m0, [rsrcq+cntq]
        pshufb               m5, m0, [ex_r]
        pshufb               m6, m0, [ex_g]
        pshufb               m0, [ex_b]
        pmulld               m2, m8
        pmulld               m3, m9
        pmulld               m4, m10
        pmulld               m5, m11
        pmulld               m6, m12
        pmulld               m0, m13
        paddd                m2, m3
        paddd                m4, m5
        paddd                m6, m0
        paddd                m2, m4
        paddd                m2, m6

        psrld                m1, 16
        psrld                m7, 16
        psrld                m2, 16

        packusdw             m1, m7
        packusdw             m2, m2
        packuswb             m1, m2
        pshufb               m1, [shuf]

        movq      [dstq+cntq+0], m1
        psrldq               m1, 8
        movd      [dstq+cntq+8], m1
        add                cntd, 12
        sub                  od, 4
    jg .loop

    add          dstq, dst_linesizeq
    add         lsrcq, l_linesizeq
    add         rsrcq, r_linesizeq
    sub       heightd, 1
    jg .nextrow
REP_RET
