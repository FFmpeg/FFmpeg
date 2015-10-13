;******************************************************************************
;* x86-SIMD-optimized IDCT for prores
;* this is identical to "simple" IDCT written by Michael Niedermayer
;* except for the clip range
;*
;* Copyright (c) 2011 Ronald S. Bultje <rsbultje@gmail.com>
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

%include "libavutil/x86/x86util.asm"

%if ARCH_X86_64

SECTION_RODATA

pw_88:      times 8 dw 0x2008
cextern pw_1
cextern pw_4
cextern pw_1019

%include "libavcodec/x86/simple_idct10_template.asm"

section .text align=16

%macro idct_put_fn 1
cglobal prores_idct_put_10, 4, 4, %1
    IDCT_PUT_FN    pw_1, 15, pw_88, 18, pw_4, pw_1019, r3
    RET
%endmacro

INIT_XMM sse2
idct_put_fn 16
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
idct_put_fn 16
%endif

%endif
