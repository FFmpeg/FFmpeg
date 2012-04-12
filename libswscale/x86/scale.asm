;******************************************************************************
;* x86-optimized horizontal line scaling functions
;* Copyright (c) 2011 Ronald S. Bultje <rsbultje@gmail.com>
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
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

SECTION_RODATA

max_19bit_int: times 4 dd 0x7ffff
max_19bit_flt: times 4 dd 524287.0
minshort:      times 8 dw 0x8000
unicoeff:      times 4 dd 0x20000000

SECTION .text

;-----------------------------------------------------------------------------
; horizontal line scaling
;
; void hscale<source_width>to<intermediate_nbits>_<filterSize>_<opt>
;                               (SwsContext *c, int{16,32}_t *dst,
;                                int dstW, const uint{8,16}_t *src,
;                                const int16_t *filter,
;                                const int32_t *filterPos, int filterSize);
;
; Scale one horizontal line. Input is either 8-bits width or 16-bits width
; ($source_width can be either 8, 9, 10 or 16, difference is whether we have to
; downscale before multiplying). Filter is 14-bits. Output is either 15bits
; (in int16_t) or 19bits (in int32_t), as given in $intermediate_nbits. Each
; output pixel is generated from $filterSize input pixels, the position of
; the first pixel is given in filterPos[nOutputPixel].
;-----------------------------------------------------------------------------

; SCALE_FUNC source_width, intermediate_nbits, filtersize, filtersuffix, n_args, n_xmm
%macro SCALE_FUNC 6
%ifnidn %3, X
cglobal hscale%1to%2_%4, %5, 7, %6, pos0, dst, w, src, filter, fltpos, pos1
%else
cglobal hscale%1to%2_%4, %5, 10, %6, pos0, dst, w, srcmem, filter, fltpos, fltsize
%endif
%if ARCH_X86_64
    movsxd        wq, wd
%define mov32 movsxd
%else ; x86-32
%define mov32 mov
%endif ; x86-64
%if %2 == 19
%if mmsize == 8 ; mmx
    mova          m2, [max_19bit_int]
%elif cpuflag(sse4)
    mova          m2, [max_19bit_int]
%else ; ssse3/sse2
    mova          m2, [max_19bit_flt]
%endif ; mmx/sse2/ssse3/sse4
%endif ; %2 == 19
%if %1 == 16
    mova          m6, [minshort]
    mova          m7, [unicoeff]
%elif %1 == 8
    pxor          m3, m3
%endif ; %1 == 8/16

%if %1 == 8
%define movlh movd
%define movbh movh
%define srcmul 1
%else ; %1 == 9-16
%define movlh movq
%define movbh movu
%define srcmul 2
%endif ; %1 == 8/9-16

%ifnidn %3, X

    ; setup loop
%if %3 == 8
    shl           wq, 1                         ; this allows *16 (i.e. now *8) in lea instructions for the 8-tap filter
%define wshr 1
%else ; %3 == 4
%define wshr 0
%endif ; %3 == 8
    lea      filterq, [filterq+wq*8]
%if %2 == 15
    lea         dstq, [dstq+wq*(2>>wshr)]
%else ; %2 == 19
    lea         dstq, [dstq+wq*(4>>wshr)]
%endif ; %2 == 15/19
    lea      fltposq, [fltposq+wq*(4>>wshr)]
    neg           wq

.loop:
%if %3 == 4 ; filterSize == 4 scaling
    ; load 2x4 or 4x4 source pixels into m0/m1
    mov32      pos0q, dword [fltposq+wq*4+ 0]   ; filterPos[0]
    mov32      pos1q, dword [fltposq+wq*4+ 4]   ; filterPos[1]
    movlh         m0, [srcq+pos0q*srcmul]       ; src[filterPos[0] + {0,1,2,3}]
%if mmsize == 8
    movlh         m1, [srcq+pos1q*srcmul]       ; src[filterPos[1] + {0,1,2,3}]
%else ; mmsize == 16
%if %1 > 8
    movhps        m0, [srcq+pos1q*srcmul]       ; src[filterPos[1] + {0,1,2,3}]
%else ; %1 == 8
    movd          m4, [srcq+pos1q*srcmul]       ; src[filterPos[1] + {0,1,2,3}]
%endif
    mov32      pos0q, dword [fltposq+wq*4+ 8]   ; filterPos[2]
    mov32      pos1q, dword [fltposq+wq*4+12]   ; filterPos[3]
    movlh         m1, [srcq+pos0q*srcmul]       ; src[filterPos[2] + {0,1,2,3}]
%if %1 > 8
    movhps        m1, [srcq+pos1q*srcmul]       ; src[filterPos[3] + {0,1,2,3}]
%else ; %1 == 8
    movd          m5, [srcq+pos1q*srcmul]       ; src[filterPos[3] + {0,1,2,3}]
    punpckldq     m0, m4
    punpckldq     m1, m5
%endif ; %1 == 8
%endif ; mmsize == 8/16
%if %1 == 8
    punpcklbw     m0, m3                        ; byte -> word
    punpcklbw     m1, m3                        ; byte -> word
%endif ; %1 == 8

    ; multiply with filter coefficients
%if %1 == 16 ; pmaddwd needs signed adds, so this moves unsigned -> signed, we'll
             ; add back 0x8000 * sum(coeffs) after the horizontal add
    psubw         m0, m6
    psubw         m1, m6
%endif ; %1 == 16
    pmaddwd       m0, [filterq+wq*8+mmsize*0]   ; *= filter[{0,1,..,6,7}]
    pmaddwd       m1, [filterq+wq*8+mmsize*1]   ; *= filter[{8,9,..,14,15}]

    ; add up horizontally (4 srcpix * 4 coefficients -> 1 dstpix)
%if mmsize == 8 ; mmx
    movq          m4, m0
    punpckldq     m0, m1
    punpckhdq     m4, m1
    paddd         m0, m4
%elif notcpuflag(ssse3) ; sse2
    mova          m4, m0
    shufps        m0, m1, 10001000b
    shufps        m4, m1, 11011101b
    paddd         m0, m4
%else ; ssse3/sse4
    phaddd        m0, m1                        ; filter[{ 0, 1, 2, 3}]*src[filterPos[0]+{0,1,2,3}],
                                                ; filter[{ 4, 5, 6, 7}]*src[filterPos[1]+{0,1,2,3}],
                                                ; filter[{ 8, 9,10,11}]*src[filterPos[2]+{0,1,2,3}],
                                                ; filter[{12,13,14,15}]*src[filterPos[3]+{0,1,2,3}]
%endif ; mmx/sse2/ssse3/sse4
%else ; %3 == 8, i.e. filterSize == 8 scaling
    ; load 2x8 or 4x8 source pixels into m0, m1, m4 and m5
    mov32      pos0q, dword [fltposq+wq*2+0]    ; filterPos[0]
    mov32      pos1q, dword [fltposq+wq*2+4]    ; filterPos[1]
    movbh         m0, [srcq+ pos0q   *srcmul]   ; src[filterPos[0] + {0,1,2,3,4,5,6,7}]
%if mmsize == 8
    movbh         m1, [srcq+(pos0q+4)*srcmul]   ; src[filterPos[0] + {4,5,6,7}]
    movbh         m4, [srcq+ pos1q   *srcmul]   ; src[filterPos[1] + {0,1,2,3}]
    movbh         m5, [srcq+(pos1q+4)*srcmul]   ; src[filterPos[1] + {4,5,6,7}]
%else ; mmsize == 16
    movbh         m1, [srcq+ pos1q   *srcmul]   ; src[filterPos[1] + {0,1,2,3,4,5,6,7}]
    mov32      pos0q, dword [fltposq+wq*2+8]    ; filterPos[2]
    mov32      pos1q, dword [fltposq+wq*2+12]   ; filterPos[3]
    movbh         m4, [srcq+ pos0q   *srcmul]   ; src[filterPos[2] + {0,1,2,3,4,5,6,7}]
    movbh         m5, [srcq+ pos1q   *srcmul]   ; src[filterPos[3] + {0,1,2,3,4,5,6,7}]
%endif ; mmsize == 8/16
%if %1 == 8
    punpcklbw     m0, m3                        ; byte -> word
    punpcklbw     m1, m3                        ; byte -> word
    punpcklbw     m4, m3                        ; byte -> word
    punpcklbw     m5, m3                        ; byte -> word
%endif ; %1 == 8

    ; multiply
%if %1 == 16 ; pmaddwd needs signed adds, so this moves unsigned -> signed, we'll
             ; add back 0x8000 * sum(coeffs) after the horizontal add
    psubw         m0, m6
    psubw         m1, m6
    psubw         m4, m6
    psubw         m5, m6
%endif ; %1 == 16
    pmaddwd       m0, [filterq+wq*8+mmsize*0]   ; *= filter[{0,1,..,6,7}]
    pmaddwd       m1, [filterq+wq*8+mmsize*1]   ; *= filter[{8,9,..,14,15}]
    pmaddwd       m4, [filterq+wq*8+mmsize*2]   ; *= filter[{16,17,..,22,23}]
    pmaddwd       m5, [filterq+wq*8+mmsize*3]   ; *= filter[{24,25,..,30,31}]

    ; add up horizontally (8 srcpix * 8 coefficients -> 1 dstpix)
%if mmsize == 8
    paddd         m0, m1
    paddd         m4, m5
    movq          m1, m0
    punpckldq     m0, m4
    punpckhdq     m1, m4
    paddd         m0, m1
%elif notcpuflag(ssse3) ; sse2
%if %1 == 8
%define mex m6
%else
%define mex m3
%endif
    ; emulate horizontal add as transpose + vertical add
    mova         mex, m0
    punpckldq     m0, m1
    punpckhdq    mex, m1
    paddd         m0, mex
    mova          m1, m4
    punpckldq     m4, m5
    punpckhdq     m1, m5
    paddd         m4, m1
    mova          m1, m0
    punpcklqdq    m0, m4
    punpckhqdq    m1, m4
    paddd         m0, m1
%else ; ssse3/sse4
    ; FIXME if we rearrange the filter in pairs of 4, we can
    ; load pixels likewise and use 2 x paddd + phaddd instead
    ; of 3 x phaddd here, faster on older cpus
    phaddd        m0, m1
    phaddd        m4, m5
    phaddd        m0, m4                        ; filter[{ 0, 1,..., 6, 7}]*src[filterPos[0]+{0,1,...,6,7}],
                                                ; filter[{ 8, 9,...,14,15}]*src[filterPos[1]+{0,1,...,6,7}],
                                                ; filter[{16,17,...,22,23}]*src[filterPos[2]+{0,1,...,6,7}],
                                                ; filter[{24,25,...,30,31}]*src[filterPos[3]+{0,1,...,6,7}]
%endif ; mmx/sse2/ssse3/sse4
%endif ; %3 == 4/8

%else ; %3 == X, i.e. any filterSize scaling

%ifidn %4, X4
%define dlt 4
%else ; %4 == X || %4 == X8
%define dlt 0
%endif ; %4 ==/!= X4
%if ARCH_X86_64
%define srcq    r8
%define pos1q   r7
%define srcendq r9
    movsxd  fltsizeq, fltsized                  ; filterSize
    lea      srcendq, [srcmemq+(fltsizeq-dlt)*srcmul] ; &src[filterSize&~4]
%else ; x86-32
%define srcq    srcmemq
%define pos1q   dstq
%define srcendq r6m
    lea        pos0q, [srcmemq+(fltsizeq-dlt)*srcmul] ; &src[filterSize&~4]
    mov      srcendq, pos0q
%endif ; x86-32/64
    lea      fltposq, [fltposq+wq*4]
%if %2 == 15
    lea         dstq, [dstq+wq*2]
%else ; %2 == 19
    lea         dstq, [dstq+wq*4]
%endif ; %2 == 15/19
    movifnidn  dstmp, dstq
    neg           wq

.loop:
    mov32      pos0q, dword [fltposq+wq*4+0]    ; filterPos[0]
    mov32      pos1q, dword [fltposq+wq*4+4]    ; filterPos[1]
    ; FIXME maybe do 4px/iteration on x86-64 (x86-32 wouldn't have enough regs)?
    pxor          m4, m4
    pxor          m5, m5
    mov         srcq, srcmemmp

.innerloop:
    ; load 2x4 (mmx) or 2x8 (sse) source pixels into m0/m1 -> m4/m5
    movbh         m0, [srcq+ pos0q     *srcmul] ; src[filterPos[0] + {0,1,2,3(,4,5,6,7)}]
    movbh         m1, [srcq+(pos1q+dlt)*srcmul] ; src[filterPos[1] + {0,1,2,3(,4,5,6,7)}]
%if %1 == 8
    punpcklbw     m0, m3
    punpcklbw     m1, m3
%endif ; %1 == 8

    ; multiply
%if %1 == 16 ; pmaddwd needs signed adds, so this moves unsigned -> signed, we'll
             ; add back 0x8000 * sum(coeffs) after the horizontal add
    psubw         m0, m6
    psubw         m1, m6
%endif ; %1 == 16
    pmaddwd       m0, [filterq]                 ; filter[{0,1,2,3(,4,5,6,7)}]
    pmaddwd       m1, [filterq+(fltsizeq+dlt)*2]; filter[filtersize+{0,1,2,3(,4,5,6,7)}]
    paddd         m4, m0
    paddd         m5, m1
    add      filterq, mmsize
    add         srcq, srcmul*mmsize/2
    cmp         srcq, srcendq                   ; while (src += 4) < &src[filterSize]
    jl .innerloop

%ifidn %4, X4
    mov32      pos1q, dword [fltposq+wq*4+4]    ; filterPos[1]
    movlh         m0, [srcq+ pos0q     *srcmul] ; split last 4 srcpx of dstpx[0]
    sub        pos1q, fltsizeq                  ; and first 4 srcpx of dstpx[1]
%if %1 > 8
    movhps        m0, [srcq+(pos1q+dlt)*srcmul]
%else ; %1 == 8
    movd          m1, [srcq+(pos1q+dlt)*srcmul]
    punpckldq     m0, m1
%endif ; %1 == 8
%if %1 == 8
    punpcklbw     m0, m3
%endif ; %1 == 8
%if %1 == 16 ; pmaddwd needs signed adds, so this moves unsigned -> signed, we'll
             ; add back 0x8000 * sum(coeffs) after the horizontal add
    psubw         m0, m6
%endif ; %1 == 16
    pmaddwd       m0, [filterq]
%endif ; %4 == X4

    lea      filterq, [filterq+(fltsizeq+dlt)*2]

%if mmsize == 8 ; mmx
    movq          m0, m4
    punpckldq     m4, m5
    punpckhdq     m0, m5
    paddd         m0, m4
%else ; mmsize == 16
%if notcpuflag(ssse3) ; sse2
    mova          m1, m4
    punpcklqdq    m4, m5
    punpckhqdq    m1, m5
    paddd         m4, m1
%else ; ssse3/sse4
    phaddd        m4, m5
%endif ; sse2/ssse3/sse4
%ifidn %4, X4
    paddd         m4, m0
%endif ; %3 == X4
%if notcpuflag(ssse3) ; sse2
    pshufd        m4, m4, 11011000b
    movhlps       m0, m4
    paddd         m0, m4
%else ; ssse3/sse4
    phaddd        m4, m4
    SWAP           0, 4
%endif ; sse2/ssse3/sse4
%endif ; mmsize == 8/16
%endif ; %3 ==/!= X

%if %1 == 16 ; add 0x8000 * sum(coeffs), i.e. back from signed -> unsigned
    paddd         m0, m7
%endif ; %1 == 16

    ; clip, store
    psrad         m0, 14 + %1 - %2
%ifidn %3, X
    movifnidn   dstq, dstmp
%endif ; %3 == X
%if %2 == 15
    packssdw      m0, m0
%ifnidn %3, X
    movh [dstq+wq*(2>>wshr)], m0
%else ; %3 == X
    movd [dstq+wq*2], m0
%endif ; %3 ==/!= X
%else ; %2 == 19
%if mmsize == 8
    PMINSD_MMX    m0, m2, m4
%elif cpuflag(sse4)
    pminsd        m0, m2
%else ; sse2/ssse3
    cvtdq2ps      m0, m0
    minps         m0, m2
    cvtps2dq      m0, m0
%endif ; mmx/sse2/ssse3/sse4
%ifnidn %3, X
    mova [dstq+wq*(4>>wshr)], m0
%else ; %3 == X
    movq [dstq+wq*4], m0
%endif ; %3 ==/!= X
%endif ; %2 == 15/19
%ifnidn %3, X
    add           wq, (mmsize<<wshr)/4          ; both 8tap and 4tap really only do 4 pixels (or for mmx: 2 pixels)
                                                ; per iteration. see "shl wq,1" above as for why we do this
%else ; %3 == X
    add           wq, 2
%endif ; %3 ==/!= X
    jl .loop
    REP_RET
%endmacro

; SCALE_FUNCS source_width, intermediate_nbits, n_xmm
%macro SCALE_FUNCS 3
SCALE_FUNC %1, %2, 4, 4,  6, %3
SCALE_FUNC %1, %2, 8, 8,  6, %3
%if mmsize == 8
SCALE_FUNC %1, %2, X, X,  7, %3
%else
SCALE_FUNC %1, %2, X, X4, 7, %3
SCALE_FUNC %1, %2, X, X8, 7, %3
%endif
%endmacro

; SCALE_FUNCS2 8_xmm_args, 9to10_xmm_args, 16_xmm_args
%macro SCALE_FUNCS2 3
%if notcpuflag(sse4)
SCALE_FUNCS  8, 15, %1
SCALE_FUNCS  9, 15, %2
SCALE_FUNCS 10, 15, %2
SCALE_FUNCS 14, 15, %2
SCALE_FUNCS 16, 15, %3
%endif ; !sse4
SCALE_FUNCS  8, 19, %1
SCALE_FUNCS  9, 19, %2
SCALE_FUNCS 10, 19, %2
SCALE_FUNCS 14, 19, %2
SCALE_FUNCS 16, 19, %3
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
SCALE_FUNCS2 0, 0, 0
%endif
INIT_XMM sse2
SCALE_FUNCS2 6, 7, 8
INIT_XMM ssse3
SCALE_FUNCS2 6, 6, 8
INIT_XMM sse4
SCALE_FUNCS2 6, 6, 8
