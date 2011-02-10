;*****************************************************************************
;* x86-optimized AC-3 DSP utils
;* Copyright (c) 2011 Justin Ruggles
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
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

SECTION .text

;-----------------------------------------------------------------------------
; void ff_ac3_exponent_min(uint8_t *exp, int num_reuse_blocks, int nb_coefs)
;-----------------------------------------------------------------------------

%macro AC3_EXPONENT_MIN 1
cglobal ac3_exponent_min_%1, 3,4,2, exp, reuse_blks, expn, offset
    shl  reuse_blksq, 8
    jz .end
    LOOP_ALIGN
.nextexp:
    mov      offsetq, reuse_blksq
    mova          m0, [expq+offsetq]
    sub      offsetq, 256
    LOOP_ALIGN
.nextblk:
    PMINUB        m0, [expq+offsetq], m1
    sub      offsetq, 256
    jae .nextblk
    mova      [expq], m0
    add         expq, mmsize
    sub        expnq, mmsize
    jg .nextexp
.end:
    REP_RET
%endmacro

%define PMINUB PMINUB_MMX
%define LOOP_ALIGN
INIT_MMX
AC3_EXPONENT_MIN mmx
%ifdef HAVE_MMX2
%define PMINUB PMINUB_MMXEXT
%define LOOP_ALIGN ALIGN 16
AC3_EXPONENT_MIN mmxext
%endif
%ifdef HAVE_SSE
INIT_XMM
AC3_EXPONENT_MIN sse2
%endif
%undef PMINUB
%undef LOOP_ALIGN
