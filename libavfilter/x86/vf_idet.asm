;*****************************************************************************
;* x86-optimized functions for idet filter
;*
;* Copyright (C) 2014 Pascal Massimino (pascal.massimino@gmail.com)
;* Copyright (c) 2014 Neil Birkbeck (birkbeck@google.com)
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

; Implementation that does 8-bytes at a time using single-word operations.
%macro IDET_FILTER_LINE 1
INIT_MMX %1
cglobal idet_filter_line, 4, 5, 0, a, b, c, width, index
    xor       indexq, indexq
%define   m_zero m2
%define   m_sum  m5
    pxor      m_sum, m_sum
    pxor      m_zero, m_zero

.loop:
    movu      m0, [aq + indexq*1]
    punpckhbw m1, m0, m_zero
    punpcklbw m0, m_zero

    movu      m3, [cq + indexq*1]
    punpckhbw m4, m3, m_zero
    punpcklbw m3, m_zero

    paddsw    m1, m4
    paddsw    m0, m3

    movu      m3, [bq + indexq*1]
    punpckhbw m4, m3, m_zero
    punpcklbw m3, m_zero

    paddw     m4, m4
    paddw     m3, m3
    psubsw    m1, m4
    psubsw    m0, m3

    ABS2      m1, m0, m4, m3

    paddw     m0, m1
    punpckhwd m1, m0, m_zero
    punpcklwd m0, m_zero

    paddd     m0, m1
    paddd     m_sum, m0

    add       indexq, 0x8
    CMP       widthd, indexd
    jg        .loop

    HADDD     m_sum, m0
    movd      eax, m_sum
    RET
%endmacro

%if ARCH_X86_32
IDET_FILTER_LINE mmxext
IDET_FILTER_LINE mmx
%endif

;******************************************************************************
; 16bit implementation that does 4/8-pixels at a time

%macro PABS_DIFF_WD 3    ; a, b, junk   , output=a
  psubusw   %3, %2, %1
  psubusw   %1, %2
  por       %1, %3

  mova      %2, %1
  punpcklwd %1, m_zero
  punpckhwd %2, m_zero
  paddd     %1, %2
%endmacro

%macro IDET_FILTER_LINE_16BIT 1   ; %1=increment (4 or 8 words)
cglobal idet_filter_line_16bit, 4, 5, 8, a, b, c, width, index
    xor       indexq, indexq
%define m_zero m1
%define m_sum  m0
    pxor      m_sum, m_sum
    pxor      m_zero, m_zero

.loop_16bit:
    movu      m2, [bq + indexq * 2]  ; B
    movu      m3, [aq + indexq * 2]  ; A
    mova      m6, m2
    psubusw   m5, m2, m3             ; ba

    movu      m4, [cq + indexq * 2]  ; C
    add       indexq, %1
    psubusw   m3, m2                 ; ab
    CMP       indexd, widthd

    psubusw   m6, m4                 ; bc
    psubusw   m4, m2                 ; cb

    PABS_DIFF_WD   m3, m6, m7        ; |ab - bc|
    PABS_DIFF_WD   m5, m4, m7        ; |ba - cb|
    paddd          m_sum, m3
    paddd          m_sum, m5
    jl        .loop_16bit

    HADDD     m_sum, m2
    movd      eax, m_sum
    RET
%endmacro

INIT_XMM sse2
IDET_FILTER_LINE_16BIT 8
%if ARCH_X86_32
INIT_MMX mmx
IDET_FILTER_LINE_16BIT 4
%endif

;******************************************************************************
; SSE2 8-bit implementation that does 16-bytes at a time:

INIT_XMM sse2
cglobal idet_filter_line, 4, 6, 7, a, b, c, width, index, total
    xor       indexq, indexq
    pxor      m0, m0
    pxor      m1, m1

.sse2_loop:
    movu      m2, [bq + indexq*1]  ; B
    movu      m3, [aq + indexq*1]  ; A
    mova      m6, m2
    mova      m4, m3
    psubusb   m5, m2, m3           ; ba

    movu      m3, [cq + indexq*1]  ; C
    add       indexq, 0x10
    psubusb   m4, m2               ; ab
    CMP       indexd, widthd

    psubusb   m6, m3               ; bc
    psubusb   m3, m2               ; cb

    psadbw    m4, m6               ; |ab - bc|
    paddq     m0, m4
    psadbw    m5, m3               ; |ba - cb|
    paddq     m1, m5
    jl       .sse2_loop

    paddq     m0, m1
    movhlps   m1, m0
    paddq     m0, m1
    movd      eax, m0
    RET
