;*****************************************************************************
;* SSSE3-optimized HEVC dequant code
;*****************************************************************************
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

INIT_XMM ssse3
; void ff_hevc_dequant_8_ssse3(int16_t *coeffs, int16_t log2_size)
cglobal hevc_dequant_8, 2, 3+UNIX64, 3

; coeffs, log2_size (in ecx), tmp/size
%if WIN64
    DECLARE_REG_TMP 1,0,2
    ; r0 is the shift register (ecx) on win64
    xchg          r0, r1
%elif ARCH_X86_64
    DECLARE_REG_TMP 0,3,1
    ; r3 is ecx
    mov          t1d, r1d
%else
    ; r1 is ecx
    DECLARE_REG_TMP 0,1,2
%endif

    mov          t2d, 256
    shl          t2d, t1b
    movd          m0, t2d
    add          t1d, t1d
    SPLATW        m0, m0
    mov          t2d, 1
    shl          t2d, t1b
.loop:
    mova          m1, [t0]
    mova          m2, [t0+mmsize]
    pmulhrsw      m1, m0
    pmulhrsw      m2, m0
    mova        [t0], m1
    mova [t0+mmsize], m2
    add           t0, 2*mmsize
    sub          t2d, mmsize
    jg         .loop
    RET
