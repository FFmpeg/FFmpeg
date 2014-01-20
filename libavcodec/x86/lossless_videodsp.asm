;******************************************************************************
;* SIMD lossless video DSP utils
;* Copyright (c) 2014 Michael Niedermayer
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

SECTION_TEXT

%macro ADD_INT16_LOOP 1 ; %1 = is_aligned
    movd      m4, maskq
    punpcklwd m4, m4
    punpcklwd m4, m4
    punpcklwd m4, m4
    add     wq, wq
    test    wq, 2*mmsize - 1
    jz %%.tomainloop
%%.wordloop:
    sub     wq, 2
    mov     ax, [srcq+wq]
    add     ax, [dstq+wq]
    and     ax, maskw
    mov     [dstq+wq], ax
    test    wq, 2*mmsize - 1
    jnz %%.wordloop
%%.tomainloop:
    add     srcq, wq
    add     dstq, wq
    neg     wq
    jz      %%.end
%%.loop:
%if %1
    mova    m0, [srcq+wq]
    mova    m1, [dstq+wq]
    mova    m2, [srcq+wq+mmsize]
    mova    m3, [dstq+wq+mmsize]
%else
    movu    m0, [srcq+wq]
    movu    m1, [dstq+wq]
    movu    m2, [srcq+wq+mmsize]
    movu    m3, [dstq+wq+mmsize]
%endif
    paddw   m0, m1
    paddw   m2, m3
    pand    m0, m4
    pand    m2, m4
%if %1
    mova    [dstq+wq]       , m0
    mova    [dstq+wq+mmsize], m2
%else
    movu    [dstq+wq]       , m0
    movu    [dstq+wq+mmsize], m2
%endif
    add     wq, 2*mmsize
    jl %%.loop
%%.end:
    RET
%endmacro

INIT_MMX mmx
cglobal add_int16, 4,4,5, dst, src, mask, w
    ADD_INT16_LOOP 1

INIT_XMM sse2
cglobal add_int16, 4,4,5, dst, src, mask, w
    test srcq, mmsize-1
    jnz .unaligned
    test dstq, mmsize-1
    jnz .unaligned
    ADD_INT16_LOOP 1
.unaligned:
    ADD_INT16_LOOP 0
