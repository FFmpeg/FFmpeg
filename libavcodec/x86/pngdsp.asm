;******************************************************************************
;* x86 optimizations for PNG decoding
;*
;* Copyright (c) 2008 Loren Merritt <lorenm@u.washington.edu>
;* Copyright (c) 2012 Ronald S. Bultje <rsbultje@gmail.com>
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

SECTION_RODATA

cextern pw_255

section .text align=16

; %1 = nr. of xmm registers used
%macro ADD_BYTES_FN 1
cglobal add_bytes_l2, 4, 6, %1, dst, src1, src2, wa, w, i
%if ARCH_X86_64
    movsxd             waq, wad
%endif
    xor                 iq, iq

    ; vector loop
    mov                 wq, waq
    and                waq, ~(mmsize*2-1)
    jmp .end_v
.loop_v:
    mova                m0, [src1q+iq]
    mova                m1, [src1q+iq+mmsize]
    paddb               m0, [src2q+iq]
    paddb               m1, [src2q+iq+mmsize]
    mova  [dstq+iq       ], m0
    mova  [dstq+iq+mmsize], m1
    add                 iq, mmsize*2
.end_v:
    cmp                 iq, waq
    jl .loop_v

%if mmsize == 16
    ; vector loop
    mov                 wq, waq
    and                waq, ~7
    jmp .end_l
.loop_l:
    movq               mm0, [src1q+iq]
    paddb              mm0, [src2q+iq]
    movq  [dstq+iq       ], mm0
    add                 iq, 8
.end_l:
    cmp                 iq, waq
    jl .loop_l
%endif

    ; scalar loop for leftover
    jmp .end_s
.loop_s:
    mov                wab, [src1q+iq]
    add                wab, [src2q+iq]
    mov          [dstq+iq], wab
    inc                 iq
.end_s:
    cmp                 iq, wq
    jl .loop_s
    REP_RET
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
ADD_BYTES_FN 0
%endif

INIT_XMM sse2
ADD_BYTES_FN 2

%macro ADD_PAETH_PRED_FN 1
cglobal add_png_paeth_prediction, 5, 7, %1, dst, src, top, w, bpp, end, cntr
%if ARCH_X86_64
    movsxd            bppq, bppd
    movsxd              wq, wd
%endif
    lea               endq, [dstq+wq-(mmsize/2-1)]
    sub               topq, dstq
    sub               srcq, dstq
    sub               dstq, bppq
    pxor                m7, m7

    PUSH              dstq
    lea              cntrq, [bppq-1]
    shr              cntrq, 2 + mmsize/16
.bpp_loop:
    lea               dstq, [dstq+cntrq*(mmsize/2)]
    movh                m0, [dstq]
    movh                m1, [topq+dstq]
    punpcklbw           m0, m7
    punpcklbw           m1, m7
    add               dstq, bppq
.loop:
    mova                m2, m1
    movh                m1, [topq+dstq]
    mova                m3, m2
    punpcklbw           m1, m7
    mova                m4, m2
    psubw               m3, m1
    psubw               m4, m0
    mova                m5, m3
    paddw               m5, m4
%if cpuflag(ssse3)
    pabsw               m3, m3
    pabsw               m4, m4
    pabsw               m5, m5
%else ; !cpuflag(ssse3)
    psubw               m7, m5
    pmaxsw              m5, m7
    pxor                m6, m6
    pxor                m7, m7
    psubw               m6, m3
    psubw               m7, m4
    pmaxsw              m3, m6
    pmaxsw              m4, m7
    pxor                m7, m7
%endif ; cpuflag(ssse3)
    mova                m6, m4
    pminsw              m6, m5
    pcmpgtw             m3, m6
    pcmpgtw             m4, m5
    mova                m6, m4
    pand                m4, m3
    pandn               m6, m3
    pandn               m3, m0
    movh                m0, [srcq+dstq]
    pand                m6, m1
    pand                m2, m4
    punpcklbw           m0, m7
    paddw               m0, m6
    paddw               m3, m2
    paddw               m0, m3
    pand                m0, [pw_255]
    mova                m3, m0
    packuswb            m3, m3
    movh            [dstq], m3
    add               dstq, bppq
    cmp               dstq, endq
    jle .loop

    mov               dstq, [rsp]
    dec              cntrq
    jge .bpp_loop
    POP               dstq
    RET
%endmacro

INIT_MMX mmx2
ADD_PAETH_PRED_FN 0

INIT_MMX ssse3
ADD_PAETH_PRED_FN 0
