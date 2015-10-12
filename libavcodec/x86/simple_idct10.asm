;******************************************************************************
;* x86-SIMD-optimized IDCT for prores
;* this is identical to "simple" IDCT written by Michael Niedermayer
;* except for the clip range
;*
;* Copyright (c) 2011 Ronald S. Bultje <rsbultje@gmail.com>
;* Copyright (c) 2015 Christophe Gisquet
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

cextern pw_16
cextern pw_1023
pd_round_12: times 4 dd 1<<(12-1)
pd_round_19: times 4 dd 1<<(19-1)

%include "libavcodec/x86/simple_idct10_template.asm"

section .text align=16

%macro idct_fn 0
cglobal simple_idct10, 1, 1, 16
    IDCT_FN    "", 12, "", 19
    RET

cglobal simple_idct10_put, 3, 3, 16
    IDCT_FN    "", 12, "", 19, 0, pw_1023
    RET
%endmacro

INIT_XMM sse2
idct_fn
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
idct_fn
%endif

%endif
