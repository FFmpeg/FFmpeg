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

;-----------------------------------------------------------------------------
; int ff_ac3_max_msb_abs_int16(const int16_t *src, int len)
;
; This function uses 2 different methods to calculate a valid result.
; 1) logical 'or' of abs of each element
;        This is used for ssse3 because of the pabsw instruction.
;        It is also used for mmx because of the lack of min/max instructions.
; 2) calculate min/max for the array, then or(abs(min),abs(max))
;        This is used for mmxext and sse2 because they have pminsw/pmaxsw.
;-----------------------------------------------------------------------------

%macro AC3_MAX_MSB_ABS_INT16 2
cglobal ac3_max_msb_abs_int16_%1, 2,2,5, src, len
    pxor        m2, m2
    pxor        m3, m3
.loop:
%ifidn %2, min_max
    mova        m0, [srcq]
    mova        m1, [srcq+mmsize]
    pminsw      m2, m0
    pminsw      m2, m1
    pmaxsw      m3, m0
    pmaxsw      m3, m1
%else ; or_abs
%ifidn %1, mmx
    mova        m0, [srcq]
    mova        m1, [srcq+mmsize]
    ABS2        m0, m1, m3, m4
%else ; ssse3
    ; using memory args is faster for ssse3
    pabsw       m0, [srcq]
    pabsw       m1, [srcq+mmsize]
%endif
    por         m2, m0
    por         m2, m1
%endif
    add       srcq, mmsize*2
    sub       lend, mmsize
    ja .loop
%ifidn %2, min_max
    ABS2        m2, m3, m0, m1
    por         m2, m3
%endif
%ifidn mmsize, 16
    movhlps     m0, m2
    por         m2, m0
%endif
    PSHUFLW     m0, m2, 0xe
    por         m2, m0
    PSHUFLW     m0, m2, 0x1
    por         m2, m0
    movd       eax, m2
    and        eax, 0xFFFF
    RET
%endmacro

INIT_MMX
%define ABS2 ABS2_MMX
%define PSHUFLW pshufw
AC3_MAX_MSB_ABS_INT16 mmx, or_abs
%define ABS2 ABS2_MMX2
AC3_MAX_MSB_ABS_INT16 mmxext, min_max
INIT_XMM
%define PSHUFLW pshuflw
AC3_MAX_MSB_ABS_INT16 sse2, min_max
%define ABS2 ABS2_SSSE3
AC3_MAX_MSB_ABS_INT16 ssse3, or_abs
