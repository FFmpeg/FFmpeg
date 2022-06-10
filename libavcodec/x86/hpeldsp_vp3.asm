;******************************************************************************
;* SIMD-optimized halfpel functions for VP3
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

; void ff_put_no_rnd_pixels8_x2_exact(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
INIT_MMX mmxext
cglobal put_no_rnd_pixels8_x2_exact, 4,5
    lea          r4, [r2*3]
    pcmpeqb      m6, m6
.loop:
    mova         m0, [r1]
    mova         m2, [r1+r2]
    mova         m1, [r1+1]
    mova         m3, [r1+r2+1]
    pxor         m0, m6
    pxor         m2, m6
    pxor         m1, m6
    pxor         m3, m6
    PAVGB        m0, m1
    PAVGB        m2, m3
    pxor         m0, m6
    pxor         m2, m6
    mova       [r0], m0
    mova    [r0+r2], m2
    mova         m0, [r1+r2*2]
    mova         m1, [r1+r2*2+1]
    mova         m2, [r1+r4]
    mova         m3, [r1+r4+1]
    pxor         m0, m6
    pxor         m1, m6
    pxor         m2, m6
    pxor         m3, m6
    PAVGB        m0, m1
    PAVGB        m2, m3
    pxor         m0, m6
    pxor         m2, m6
    mova  [r0+r2*2], m0
    mova    [r0+r4], m2
    lea          r1, [r1+r2*4]
    lea          r0, [r0+r2*4]
    sub         r3d, 4
    jg .loop
    REP_RET


; void ff_put_no_rnd_pixels8_y2_exact(uint8_t *block, const uint8_t *pixels, ptrdiff_t line_size, int h)
INIT_MMX mmxext
cglobal put_no_rnd_pixels8_y2_exact, 4,5
    lea          r4, [r2*3]
    mova         m0, [r1]
    pcmpeqb      m6, m6
    add          r1, r2
    pxor         m0, m6
.loop:
    mova         m1, [r1]
    mova         m2, [r1+r2]
    pxor         m1, m6
    pxor         m2, m6
    PAVGB        m0, m1
    PAVGB        m1, m2
    pxor         m0, m6
    pxor         m1, m6
    mova       [r0], m0
    mova    [r0+r2], m1
    mova         m1, [r1+r2*2]
    mova         m0, [r1+r4]
    pxor         m1, m6
    pxor         m0, m6
    PAVGB        m2, m1
    PAVGB        m1, m0
    pxor         m2, m6
    pxor         m1, m6
    mova  [r0+r2*2], m2
    mova    [r0+r4], m1
    lea          r1, [r1+r2*4]
    lea          r0, [r0+r2*4]
    sub         r3d, 4
    jg .loop
    REP_RET
