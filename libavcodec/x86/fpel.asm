;******************************************************************************
;* SIMD-optimized fullpel functions
;* Copyright (c) 2008 Loren Merritt
;* Copyright (c) 2003-2013 Michael Niedermayer
;* Copyright (c) 2013 Daniel Kang
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

%macro PAVGB_MMX 4
    LOAD   %3, %1
    por    %3, %2
    pxor   %2, %1
    pand   %2, %4
    psrlq  %2, 1
    psubb  %3, %2
    SWAP   %2, %3
%endmacro

; void ff_put/avg_pixels(uint8_t *block, const uint8_t *pixels,
;                        ptrdiff_t line_size, int h)
%macro OP_PIXELS 2
%if %2 == mmsize/2
%define LOAD movh
%define SAVE movh
%define LEN  mmsize
%else
%define LOAD movu
%define SAVE mova
%define LEN  %2
%endif
cglobal %1_pixels%2, 4,5,4
    lea          r4, [r2*3]
%ifidn %1, avg
%if notcpuflag(mmxext)
    pcmpeqd      m6, m6
    paddb        m6, m6
%endif
%endif
.loop:
%assign %%i 0
%rep LEN/mmsize
    LOAD         m0, [r1 + %%i]
    LOAD         m1, [r1+r2 + %%i]
    LOAD         m2, [r1+r2*2 + %%i]
    LOAD         m3, [r1+r4 + %%i]
%ifidn %1, avg
%if notcpuflag(mmxext)
    PAVGB_MMX    [r0 + %%i], m0, m4, m6
    PAVGB_MMX    [r0+r2 + %%i], m1, m5, m6
    PAVGB_MMX    [r0+r2*2 + %%i], m2, m4, m6
    PAVGB_MMX    [r0+r4 + %%i], m3, m5, m6
%else
    pavgb        m0, [r0 + %%i]
    pavgb        m1, [r0+r2 + %%i]
    pavgb        m2, [r0+r2*2 + %%i]
    pavgb        m3, [r0+r4 + %%i]
%endif
%endif
    SAVE       [r0 + %%i], m0
    SAVE    [r0+r2 + %%i], m1
    SAVE  [r0+r2*2 + %%i], m2
    SAVE    [r0+r4 + %%i], m3
%assign %%i %%i+mmsize
%endrep
    sub         r3d, 4
    lea          r1, [r1+r2*4]
    lea          r0, [r0+r2*4]
    jne       .loop
    RET
%endmacro

INIT_MMX mmx
OP_PIXELS put, 4
OP_PIXELS put, 8
OP_PIXELS avg, 8
OP_PIXELS put, 16
OP_PIXELS avg, 16

INIT_MMX mmxext
OP_PIXELS avg, 4
OP_PIXELS avg, 8
OP_PIXELS avg, 16

INIT_XMM sse2
OP_PIXELS put, 16
OP_PIXELS avg, 16
