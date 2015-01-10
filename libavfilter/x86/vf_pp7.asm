;*****************************************************************************
;* x86-optimized functions for pp7 filter
;*
;* Copyright (c) 2005 Michael Niedermayer <michaelni@gmx.at>
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
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION .text

INIT_MMX mmx

;void ff_pp7_dctB_mmx(int16_t *dst, int16_t *src)
cglobal pp7_dctB, 2, 2, 0, dst, src
    movq   m0, [srcq]
    movq   m1, [srcq+mmsize*1]
    paddw  m0, [srcq+mmsize*6]
    paddw  m1, [srcq+mmsize*5]
    movq   m2, [srcq+mmsize*2]
    movq   m3, [srcq+mmsize*3]
    paddw  m2, [srcq+mmsize*4]
    paddw  m3, m3
    movq   m4, m3
    psubw  m3, m0
    paddw  m4, m0
    movq   m0, m2
    psubw  m2, m1
    paddw  m0, m1
    movq   m1, m4
    psubw  m4, m0
    paddw  m1, m0
    movq   m0, m3
    psubw  m3, m2
    psubw  m3, m2
    paddw  m2, m0
    paddw  m2, m0
    movq   [dstq], m1
    movq   [dstq+mmsize*2], m4
    movq   [dstq+mmsize*1], m2
    movq   [dstq+mmsize*3], m3
    RET
