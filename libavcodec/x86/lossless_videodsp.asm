;******************************************************************************
;* SIMD lossless video DSP utils
;* Copyright (c) 2008 Loren Merritt
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

SECTION_RODATA

pb_ef: times 8 db 14,15
pb_67: times 8 db  6, 7
pb_zzzz2323zzzzabab: db -1,-1,-1,-1, 2, 3, 2, 3,-1,-1,-1,-1,10,11,10,11
pb_zzzzzzzz67676767: db -1,-1,-1,-1,-1,-1,-1,-1, 6, 7, 6, 7, 6, 7, 6, 7

SECTION_TEXT

%macro ADD_INT16_LOOP 1 ; %1 = is_aligned
    movd      m4, maskq
    SPLATW  m4, m4
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

%macro ADD_HFYU_LEFT_LOOP_INT16 2 ; %1 = dst_is_aligned, %2 = src_is_aligned
    add     wq, wq
    add     srcq, wq
    add     dstq, wq
    neg     wq
%%.loop:
%if %2
    mova    m1, [srcq+wq]
%else
    movu    m1, [srcq+wq]
%endif
    mova    m2, m1
    pslld   m1, 16
    paddw   m1, m2
    mova    m2, m1

    pshufb  m1, m3
    paddw   m1, m2
    pshufb  m0, m5
%if mmsize == 16
    mova    m2, m1
    pshufb  m1, m4
    paddw   m1, m2
%endif
    paddw   m0, m1
    pand    m0, m7
%if %1
    mova    [dstq+wq], m0
%else
    movq    [dstq+wq], m0
    movhps  [dstq+wq+8], m0
%endif
    add     wq, mmsize
    jl %%.loop
    mov     eax, mmsize-1
    sub     eax, wd
    mov     wd, eax
    shl     wd, 8
    lea     eax, [wd+eax-1]
    movd    m1, eax
    pshufb  m0, m1
    movd    eax, m0
    RET
%endmacro

; int add_hfyu_left_prediction_int16(uint16_t *dst, const uint16_t *src, unsigned mask, int w, int left)
INIT_MMX ssse3
cglobal add_hfyu_left_prediction_int16, 4,4,8, dst, src, mask, w, left
.skip_prologue:
    mova    m5, [pb_67]
    mova    m3, [pb_zzzz2323zzzzabab]
    movd    m0, leftm
    psllq   m0, 48
    movd    m7, maskm
    SPLATW  m7 ,m7
    ADD_HFYU_LEFT_LOOP_INT16 1, 1

INIT_XMM sse4
cglobal add_hfyu_left_prediction_int16, 4,4,8, dst, src, mask, w, left
    mova    m5, [pb_ef]
    mova    m4, [pb_zzzzzzzz67676767]
    mova    m3, [pb_zzzz2323zzzzabab]
    movd    m0, leftm
    pslldq  m0, 14
    movd    m7, maskm
    SPLATW  m7 ,m7
    test    srcq, 15
    jnz .src_unaligned
    test    dstq, 15
    jnz .dst_unaligned
    ADD_HFYU_LEFT_LOOP_INT16 1, 1
.dst_unaligned:
    ADD_HFYU_LEFT_LOOP_INT16 0, 1
.src_unaligned:
    ADD_HFYU_LEFT_LOOP_INT16 0, 0
