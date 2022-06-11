;******************************************************************************
;* Vorbis x86 optimizations
;* Copyright (C) 2006 Loren Merritt <lorenm@u.washington.edu>
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

SECTION_RODATA

pdw_80000000: times 4 dd 0x80000000

SECTION .text

INIT_XMM sse
cglobal vorbis_inverse_coupling, 3, 3, 6, mag, ang, block_size
    mova                     m5, [pdw_80000000]
    shl             block_sized, 2
    add                    magq, block_sizeq
    add                    angq, block_sizeq
    neg             block_sizeq

align 16
.loop:
    mova                     m0, [magq+block_sizeq]
    mova                     m1, [angq+block_sizeq]
    xorps                    m2, m2
    xorps                    m3, m3
    cmpleps                  m2, m0     ; m <= 0.0
    cmpleps                  m3, m1     ; a <= 0.0
    andps                    m2, m5     ; keep only the sign bit
    xorps                    m1, m2
    mova                     m4, m3
    andps                    m3, m1
    andnps                   m4, m1
    addps                    m3, m0     ; a = m + ((a < 0) & (a ^ sign(m)))
    subps                    m0, m4     ; m = m + ((a > 0) & (a ^ sign(m)))
    mova     [angq+block_sizeq], m3
    mova     [magq+block_sizeq], m0
    add             block_sizeq, mmsize
    jl .loop
    RET
