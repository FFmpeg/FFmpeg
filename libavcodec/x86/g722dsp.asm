;******************************************************************************
;* SIMD optimized DSP functions for G722 coding
;*
;* Copyright (c) 2014 James Almer
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

pw_qmf_coeffs:  dw   3, -210,  -11, -805,  -11,  951,  53, 3876
pw_qmf_coeffs2: dw  12, 3876, -156,  951,   32, -805, 362, -210
pw_qmf_coeffs3: dw 362,    0 ,  32,    0, -156,    0,  12,    0
pw_qmf_coeffs4: dw  53,    0,  -11,    0,  -11,    0,   3,    0

SECTION_TEXT

INIT_XMM sse2
cglobal g722_apply_qmf, 2, 2, 5, prev, out
    movu m0, [prevq+mmsize*0]
    movu m1, [prevq+mmsize*1]
    movu m2, [prevq+mmsize*2]
    punpcklwd m3, m0, m1
    punpckhwd m0, m1
    punpcklwd m4, m2, m2
    punpckhwd m2, m2
    pmaddwd   m3, [pw_qmf_coeffs ]
    pmaddwd   m0, [pw_qmf_coeffs2]
    pmaddwd   m4, [pw_qmf_coeffs3]
    pmaddwd   m2, [pw_qmf_coeffs4]
    paddd     m0, m3
    paddd     m2, m4
    paddd     m0, m2
    pshufd    m2, m0, q0032
    paddd     m0, m2
    pshufd    m0, m0, q0001
    movq  [outq], m0
    RET
