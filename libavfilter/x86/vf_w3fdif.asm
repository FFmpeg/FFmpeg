;*****************************************************************************
;* x86-optimized functions for w3fdif filter
;*
;* Copyright (c) 2015 Paul B Mahol
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
cglobal w3fdif_scale, 3, 3, 2, 0, out_pixel, work_pixel, linesize
.loop:
    mova                         m0, [work_pixelq]
    mova                         m1, [work_pixelq+mmsize]
    psrad                        m0, 15
    psrad                        m1, 15
    packssdw                     m0, m1
    packuswb                     m0, m0
    movh               [out_pixelq], m0
    add                  out_pixelq, mmsize/2
    add                 work_pixelq, mmsize*2
    sub                   linesized, mmsize/2
    jg .loop
RET

cglobal w3fdif_simple_low, 4, 5, 6, 0, work_line, in_lines_cur0, coef, linesize, offset
    movd                  m1, [coefq]
    DEFINE_ARGS    work_line, in_lines_cur0, in_lines_cur1, linesize, offset
    SPLATW                m0, m1, 0
    SPLATW                m1, m1, 1
    pxor                  m4, m4
    mov              offsetq, 0
    mov       in_lines_cur1q, [in_lines_cur0q + gprsize]
    mov       in_lines_cur0q, [in_lines_cur0q]

.loop:
    movh                                   m2, [in_lines_cur0q+offsetq]
    movh                                   m3, [in_lines_cur1q+offsetq]
    punpcklbw                              m2, m4
    punpcklbw                              m3, m4
    SBUTTERFLY                             wd, 2, 3, 5
    pmaddwd                                m2, m0
    pmaddwd                                m3, m1
    mova               [work_lineq+offsetq*4], m2
    mova        [work_lineq+offsetq*4+mmsize], m3
    add                               offsetq, mmsize/2
    sub                             linesized, mmsize/2
    jg .loop
RET

cglobal w3fdif_complex_low, 4, 7, 8, 0, work_line, in_lines_cur0, coef, linesize
    movq                  m0, [coefq]
    DEFINE_ARGS    work_line, in_lines_cur0, in_lines_cur1, linesize, offset, in_lines_cur2, in_lines_cur3
    pshufd                m2, m0, q1111
    SPLATD                m0
    pxor                  m1, m1
    mov              offsetq, 0
    mov       in_lines_cur3q, [in_lines_cur0q+gprsize*3]
    mov       in_lines_cur2q, [in_lines_cur0q+gprsize*2]
    mov       in_lines_cur1q, [in_lines_cur0q+gprsize]
    mov       in_lines_cur0q, [in_lines_cur0q]

.loop:
    movh                                   m4, [in_lines_cur0q+offsetq]
    movh                                   m5, [in_lines_cur1q+offsetq]
    punpcklbw                              m4, m1
    punpcklbw                              m5, m1
    SBUTTERFLY                             wd, 4, 5, 7
    pmaddwd                                m4, m0
    pmaddwd                                m5, m0
    movh                                   m6, [in_lines_cur2q+offsetq]
    movh                                   m3, [in_lines_cur3q+offsetq]
    punpcklbw                              m6, m1
    punpcklbw                              m3, m1
    SBUTTERFLY                             wd, 6, 3, 7
    pmaddwd                                m6, m2
    pmaddwd                                m3, m2
    paddd                                  m4, m6
    paddd                                  m5, m3
    mova               [work_lineq+offsetq*4], m4
    mova        [work_lineq+offsetq*4+mmsize], m5
    add                               offsetq, mmsize/2
    sub                             linesized, mmsize/2
    jg .loop
RET

%if ARCH_X86_64
cglobal w3fdif_simple_high, 5, 9, 8, 0, work_line, in_lines_cur0, in_lines_adj0, coef, linesize
%else
cglobal w3fdif_simple_high, 4, 7, 8, 0, work_line, in_lines_cur0, in_lines_adj0, coef, linesize
%endif
    movq                  m2, [coefq]
%if ARCH_X86_64
    DEFINE_ARGS    work_line, in_lines_cur0, in_lines_adj0, in_lines_cur1, linesize, offset, in_lines_cur2, in_lines_adj1, in_lines_adj2
    xor              offsetq, offsetq
%else
    DEFINE_ARGS    work_line, in_lines_cur0, in_lines_adj0, in_lines_cur1, in_lines_cur2, in_lines_adj1, in_lines_adj2
    %define linesized r4mp
%endif

    pshufd                m0, m2, q0000
    SPLATW                m2, m2, 2
    pxor                  m7, m7
    mov       in_lines_cur2q, [in_lines_cur0q+gprsize*2]
    mov       in_lines_cur1q, [in_lines_cur0q+gprsize]
    mov       in_lines_cur0q, [in_lines_cur0q]
    mov       in_lines_adj2q, [in_lines_adj0q+gprsize*2]
    mov       in_lines_adj1q, [in_lines_adj0q+gprsize]
    mov       in_lines_adj0q, [in_lines_adj0q]

%if ARCH_X86_32
    sub in_lines_cur1q, in_lines_cur0q
    sub in_lines_cur2q, in_lines_cur0q
    sub in_lines_adj0q, in_lines_cur0q
    sub in_lines_adj1q, in_lines_cur0q
    sub in_lines_adj2q, in_lines_cur0q
    %define offsetq in_lines_cur0q
%endif

.loop:
%if ARCH_X86_64
    movh                                   m3, [in_lines_cur0q+offsetq]
%else
    movh                                   m3, [in_lines_cur0q]
%endif
    movh                                   m4, [in_lines_cur1q+offsetq]
    punpcklbw                              m3, m7
    punpcklbw                              m4, m7
    SBUTTERFLY                             wd, 3, 4, 1
    pmaddwd                                m3, m0
    pmaddwd                                m4, m0
    movh                                   m5, [in_lines_adj0q+offsetq]
    movh                                   m6, [in_lines_adj1q+offsetq]
    punpcklbw                              m5, m7
    punpcklbw                              m6, m7
    SBUTTERFLY                             wd, 5, 6, 1
    pmaddwd                                m5, m0
    pmaddwd                                m6, m0
    paddd                                  m3, m5
    paddd                                  m4, m6
    movh                                   m5, [in_lines_cur2q+offsetq]
    movh                                   m6, [in_lines_adj2q+offsetq]
    punpcklbw                              m5, m7
    punpcklbw                              m6, m7
    SBUTTERFLY                             wd, 5, 6, 1
    pmaddwd                                m5, m2
    pmaddwd                                m6, m2
    paddd                                  m3, m5
    paddd                                  m4, m6
%if ARCH_X86_64
    paddd                                  m3, [work_lineq+offsetq*4]
    paddd                                  m4, [work_lineq+offsetq*4+mmsize]
    mova               [work_lineq+offsetq*4], m3
    mova        [work_lineq+offsetq*4+mmsize], m4
%else
    paddd                                  m3, [work_lineq]
    paddd                                  m4, [work_lineq+mmsize]
    mova                         [work_lineq], m3
    mova                  [work_lineq+mmsize], m4
    add                            work_lineq, mmsize*2
%endif
    add                               offsetq, mmsize/2
    sub                             linesized, mmsize/2
    jg .loop
RET

%if ARCH_X86_64

cglobal w3fdif_complex_high, 5, 13, 10, 0, work_line, in_lines_cur0, in_lines_adj0, coef, linesize
    movq                  m0, [coefq+0]
    movd                  m4, [coefq+8]
    DEFINE_ARGS    work_line, in_lines_cur0, in_lines_adj0, in_lines_cur1, linesize, offset, in_lines_cur2, in_lines_cur3, in_lines_cur4, in_lines_adj1, in_lines_adj2, in_lines_adj3, in_lines_adj4
    pshufd                m1, m0, q1111
    SPLATD                m0
    SPLATW                m4, m4
    pxor                  m3, m3
    mov              offsetq, 0
    mov       in_lines_cur4q, [in_lines_cur0q+gprsize*4]
    mov       in_lines_cur3q, [in_lines_cur0q+gprsize*3]
    mov       in_lines_cur2q, [in_lines_cur0q+gprsize*2]
    mov       in_lines_cur1q, [in_lines_cur0q+gprsize]
    mov       in_lines_cur0q, [in_lines_cur0q]
    mov       in_lines_adj4q, [in_lines_adj0q+gprsize*4]
    mov       in_lines_adj3q, [in_lines_adj0q+gprsize*3]
    mov       in_lines_adj2q, [in_lines_adj0q+gprsize*2]
    mov       in_lines_adj1q, [in_lines_adj0q+gprsize]
    mov       in_lines_adj0q, [in_lines_adj0q]

.loop:
    movh                                   m5, [in_lines_cur0q+offsetq]
    movh                                   m6, [in_lines_cur1q+offsetq]
    punpcklbw                              m5, m3
    punpcklbw                              m6, m3
    SBUTTERFLY                             wd, 5, 6, 2
    pmaddwd                                m5, m0
    pmaddwd                                m6, m0
    movh                                   m8, [in_lines_cur2q+offsetq]
    movh                                   m9, [in_lines_cur3q+offsetq]
    punpcklbw                              m8, m3
    punpcklbw                              m9, m3
    SBUTTERFLY                             wd, 8, 9, 2
    pmaddwd                                m8, m1
    pmaddwd                                m9, m1
    paddd                                  m5, m8
    paddd                                  m6, m9
    movh                                   m8, [in_lines_adj0q+offsetq]
    movh                                   m9, [in_lines_adj1q+offsetq]
    punpcklbw                              m8, m3
    punpcklbw                              m9, m3
    SBUTTERFLY                             wd, 8, 9, 2
    pmaddwd                                m8, m0
    pmaddwd                                m9, m0
    paddd                                  m5, m8
    paddd                                  m6, m9
    movh                                   m8, [in_lines_adj2q+offsetq]
    movh                                   m9, [in_lines_adj3q+offsetq]
    punpcklbw                              m8, m3
    punpcklbw                              m9, m3
    SBUTTERFLY                             wd, 8, 9, 2
    pmaddwd                                m8, m1
    pmaddwd                                m9, m1
    paddd                                  m5, m8
    paddd                                  m6, m9
    movh                                   m8, [in_lines_cur4q+offsetq]
    movh                                   m9, [in_lines_adj4q+offsetq]
    punpcklbw                              m8, m3
    punpcklbw                              m9, m3
    SBUTTERFLY                             wd, 8, 9, 2
    pmaddwd                                m8, m4
    pmaddwd                                m9, m4
    paddd                                  m5, m8
    paddd                                  m6, m9
    paddd                                  m5, [work_lineq+offsetq*4]
    paddd                                  m6, [work_lineq+offsetq*4+mmsize]
    mova               [work_lineq+offsetq*4], m5
    mova        [work_lineq+offsetq*4+mmsize], m6
    add                               offsetq, mmsize/2
    sub                             linesized, mmsize/2
    jg .loop
RET

%endif
