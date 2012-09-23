;*****************************************************************************
;* x86-optimized functions for volume filter
;* Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86inc.asm"

SECTION_RODATA 32

pw_1:   times 8 dw 1
pw_128: times 8 dw 128

SECTION_TEXT

;------------------------------------------------------------------------------
; void ff_scale_samples_s16(uint8_t *dst, const uint8_t *src, int len,
;                           int volume)
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal scale_samples_s16, 4,4,4, dst, src, len, volume
    movd        m0, volumem
    pshuflw     m0, m0, 0
    punpcklwd   m0, [pw_1]
    mova        m1, [pw_128]
    lea       lenq, [lend*2-mmsize]
.loop:
    ; dst[i] = av_clip_int16((src[i] * volume + 128) >> 8);
    mova        m2, [srcq+lenq]
    punpcklwd   m3, m2, m1
    punpckhwd   m2, m1
    pmaddwd     m3, m0
    pmaddwd     m2, m0
    psrad       m3, 8
    psrad       m2, 8
    packssdw    m3, m2
    mova  [dstq+lenq], m3
    sub       lenq, mmsize
    jge .loop
    REP_RET
