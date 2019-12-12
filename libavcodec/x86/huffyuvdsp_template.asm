;******************************************************************************
;* SIMD-optimized HuffYUV functions
;* Copyright (c) 2008 Loren Merritt
;* Copyright (c) 2014 Christophe Gisquet
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

%macro INT16_LOOP 2 ; %1 = a/u (aligned/unaligned), %2 = add/sub
    movd    xm4, maskd
    SPLATW  m4, xm4
    add     wd, wd
    test    wq, 2*mmsize - 1
    jz %%.tomainloop
    push  tmpq
%%.wordloop:
    sub     wq, 2
%ifidn %2, add
    mov   tmpw, [srcq+wq]
    add   tmpw, [dstq+wq]
%else
    mov   tmpw, [src1q+wq]
    sub   tmpw, [src2q+wq]
%endif
    and   tmpw, maskw
    mov     [dstq+wq], tmpw
    test    wq, 2*mmsize - 1
    jnz %%.wordloop
    pop   tmpq
%%.tomainloop:
%ifidn %2, add
    add     srcq, wq
%else
    add     src1q, wq
    add     src2q, wq
%endif
    add     dstq, wq
    neg     wq
    jz      %%.end
%%.loop:
%ifidn %2, add
    mov%1   m0, [srcq+wq]
    mov%1   m1, [dstq+wq]
    mov%1   m2, [srcq+wq+mmsize]
    mov%1   m3, [dstq+wq+mmsize]
%else
    mov%1   m0, [src1q+wq]
    mov%1   m1, [src2q+wq]
    mov%1   m2, [src1q+wq+mmsize]
    mov%1   m3, [src2q+wq+mmsize]
%endif
    p%2w    m0, m1
    p%2w    m2, m3
    pand    m0, m4
    pand    m2, m4
    mov%1   [dstq+wq]       , m0
    mov%1   [dstq+wq+mmsize], m2
    add     wq, 2*mmsize
    jl %%.loop
%%.end:
    RET
%endmacro
