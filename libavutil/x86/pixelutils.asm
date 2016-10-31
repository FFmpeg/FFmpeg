;******************************************************************************
;* Pixel utilities SIMD
;*
;* Copyright (C) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
;* Copyright (C) 2014 Clément Bœsch <u pkh me>
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

%include "x86util.asm"

SECTION .text

;-------------------------------------------------------------------------------
; int ff_pixelutils_sad_8x8_mmx(const uint8_t *src1, ptrdiff_t stride1,
;                               const uint8_t *src2, ptrdiff_t stride2);
;-------------------------------------------------------------------------------
INIT_MMX mmx
cglobal pixelutils_sad_8x8, 4,4,0, src1, stride1, src2, stride2
    pxor        m7, m7
    pxor        m6, m6
%rep 4
    mova        m0, [src1q]
    mova        m2, [src1q + stride1q]
    mova        m1, [src2q]
    mova        m3, [src2q + stride2q]
    psubusb     m4, m0, m1
    psubusb     m5, m2, m3
    psubusb     m1, m0
    psubusb     m3, m2
    por         m1, m4
    por         m3, m5
    punpcklbw   m0, m1, m7
    punpcklbw   m2, m3, m7
    punpckhbw   m1, m7
    punpckhbw   m3, m7
    paddw       m0, m1
    paddw       m2, m3
    paddw       m0, m2
    paddw       m6, m0
    lea         src1q, [src1q + 2*stride1q]
    lea         src2q, [src2q + 2*stride2q]
%endrep
    psrlq       m0, m6, 32
    paddw       m6, m0
    psrlq       m0, m6, 16
    paddw       m6, m0
    movd        eax, m6
    movzx       eax, ax
    RET

;-------------------------------------------------------------------------------
; int ff_pixelutils_sad_8x8_mmxext(const uint8_t *src1, ptrdiff_t stride1,
;                                  const uint8_t *src2, ptrdiff_t stride2);
;-------------------------------------------------------------------------------
INIT_MMX mmxext
cglobal pixelutils_sad_8x8, 4,4,0, src1, stride1, src2, stride2
    pxor        m2, m2
%rep 4
    mova        m0, [src1q]
    mova        m1, [src1q + stride1q]
    psadbw      m0, [src2q]
    psadbw      m1, [src2q + stride2q]
    paddw       m2, m0
    paddw       m2, m1
    lea         src1q, [src1q + 2*stride1q]
    lea         src2q, [src2q + 2*stride2q]
%endrep
    movd        eax, m2
    RET

;-------------------------------------------------------------------------------
; int ff_pixelutils_sad_16x16_mmxext(const uint8_t *src1, ptrdiff_t stride1,
;                                    const uint8_t *src2, ptrdiff_t stride2);
;-------------------------------------------------------------------------------
INIT_MMX mmxext
cglobal pixelutils_sad_16x16, 4,4,0, src1, stride1, src2, stride2
    pxor        m2, m2
%rep 16
    mova        m0, [src1q]
    mova        m1, [src1q + 8]
    psadbw      m0, [src2q]
    psadbw      m1, [src2q + 8]
    paddw       m2, m0
    paddw       m2, m1
    add         src1q, stride1q
    add         src2q, stride2q
%endrep
    movd        eax, m2
    RET

;-------------------------------------------------------------------------------
; int ff_pixelutils_sad_16x16_sse(const uint8_t *src1, ptrdiff_t stride1,
;                                 const uint8_t *src2, ptrdiff_t stride2);
;-------------------------------------------------------------------------------
INIT_XMM sse2
cglobal pixelutils_sad_16x16, 4,4,5, src1, stride1, src2, stride2
    movu        m4, [src1q]
    movu        m2, [src2q]
    movu        m1, [src1q + stride1q]
    movu        m3, [src2q + stride2q]
    psadbw      m4, m2
    psadbw      m1, m3
    paddw       m4, m1
%rep 7
    lea         src1q, [src1q + 2*stride1q]
    lea         src2q, [src2q + 2*stride2q]
    movu        m0, [src1q]
    movu        m2, [src2q]
    movu        m1, [src1q + stride1q]
    movu        m3, [src2q + stride2q]
    psadbw      m0, m2
    psadbw      m1, m3
    paddw       m4, m0
    paddw       m4, m1
%endrep
    movhlps     m0, m4
    paddw       m4, m0
    movd        eax, m4
    RET

;-------------------------------------------------------------------------------
; int ff_pixelutils_sad_[au]_16x16_sse(const uint8_t *src1, ptrdiff_t stride1,
;                                      const uint8_t *src2, ptrdiff_t stride2);
;-------------------------------------------------------------------------------
%macro SAD_XMM_16x16 1
INIT_XMM sse2
cglobal pixelutils_sad_%1_16x16, 4,4,3, src1, stride1, src2, stride2
    mov%1       m2, [src2q]
    psadbw      m2, [src1q]
    mov%1       m1, [src2q + stride2q]
    psadbw      m1, [src1q + stride1q]
    paddw       m2, m1
%rep 7
    lea         src1q, [src1q + 2*stride1q]
    lea         src2q, [src2q + 2*stride2q]
    mov%1       m0, [src2q]
    psadbw      m0, [src1q]
    mov%1       m1, [src2q + stride2q]
    psadbw      m1, [src1q + stride1q]
    paddw       m2, m0
    paddw       m2, m1
%endrep
    movhlps     m0, m2
    paddw       m2, m0
    movd        eax, m2
    RET
%endmacro

SAD_XMM_16x16 a
SAD_XMM_16x16 u
