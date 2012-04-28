;******************************************************************************
;* Copyright (c) 2012 Michael Niedermayer
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

%include "libavutil/x86/x86inc.asm"
%include "libavutil/x86/x86util.asm"

SECTION .text

%macro INT16_TO_INT32 1
cglobal int16_to_int32_%1, 3, 3, 0, dst, src, len
    mov srcq, [srcq]
    mov dstq, [dstq]
%ifidn %1, a
    test dstq, mmsize-1
        jne int16_to_int32_u_int %+ SUFFIX
    test srcq, mmsize-1
        jne int16_to_int32_u_int %+ SUFFIX
%else
int16_to_int32_u_int %+ SUFFIX
%endif
.next
    mov%1 m4, [srcq]
    pxor m0, m0
    pxor m1, m1
    punpcklwd m0, m4
    punpckhwd m1, m4
    mov%1 [         dstq], m0
    mov%1 [mmsize + dstq], m1
    add srcq, mmsize
    add dstq, 2*mmsize
    sub lenq, 2*mmsize
        jg .next
%if mmsize == 8
    emms
%endif
    REP_RET
%endmacro

INIT_MMX mmx
INT16_TO_INT32 u
INT16_TO_INT32 a

INIT_XMM sse
INT16_TO_INT32 u
INT16_TO_INT32 a
