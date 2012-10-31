;******************************************************************************
;* x86 optimized dithering format conversion
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

%include "libavutil/x86/x86util.asm"

SECTION_RODATA 32

pf_s16_scale: times 4 dd 32753.0

SECTION_TEXT

;------------------------------------------------------------------------------
; void ff_quantize(int16_t *dst, float *src, float *dither, int len);
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal quantize, 4,4,3, dst, src, dither, len
    lea         lenq, [2*lend]
    add         dstq, lenq
    lea         srcq, [srcq+2*lenq]
    lea      ditherq, [ditherq+2*lenq]
    neg         lenq
    mova          m2, [pf_s16_scale]
.loop:
    mulps         m0, m2, [srcq+2*lenq]
    mulps         m1, m2, [srcq+2*lenq+mmsize]
    addps         m0, [ditherq+2*lenq]
    addps         m1, [ditherq+2*lenq+mmsize]
    cvtps2dq      m0, m0
    cvtps2dq      m1, m1
    packssdw      m0, m1
    mova     [dstq+lenq], m0
    add         lenq, mmsize
    jl .loop
    REP_RET
