;******************************************************************************
;* x86-optimized horizontal/vertical line scaling functions
;* Copyright (c) 2011 Ronald S. Bultje <rsbultje@gmail.com>
;*                    Kieran Kunhya <kieran@kunhya.com>
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
yuv2yuvX_16_start:  times 4 dd 0x4000 - 0x40000000
yuv2yuvX_10_start:  times 4 dd 0x10000
yuv2yuvX_9_start:   times 4 dd 0x20000
yuv2yuvX_10_upper:  times 8 dw 0x3ff
yuv2yuvX_9_upper:   times 8 dw 0x1ff
pd_4:          times 4 dd 4
pd_4min0x40000:times 4 dd 4 - (0x40000)
pw_16:         times 8 dw 16
pw_32:         times 8 dw 32
pw_512:        times 8 dw 512
pw_1024:       times 8 dw 1024

SECTION .text

;-----------------------------------------------------------------------------
; horizontal line scaling
;
; void hscale<source_width>to<intermediate_nbits>_<filterSize>_<opt>
;                               (SwsContext *c, int{16,32}_t *dst,
;                                int dstW, const uint{8,16}_t *src,
;                                const int16_t *filter,
;                                const int16_t *filterPos, int filterSize);
;
; Scale one horizontal line. Input is either 8-bits width or 16-bits width
; ($source_width can be either 8, 9, 10 or 16, difference is whether we have to
; downscale before multiplying). Filter is 14-bits. Output is either 15bits
; (in int16_t) or 19bits (in int32_t), as given in $intermediate_nbits. Each
; output pixel is generated from $filterSize input pixels, the position of
; the first pixel is given in filterPos[nOutputPixel].
;-----------------------------------------------------------------------------

; SCALE_FUNC source_width, intermediate_nbits, filtersize, filtersuffix, opt, n_args, n_xmm
%macro SCALE_FUNC 7
cglobal hscale%1to%2_%4_%5, %6, 7, %7
%ifdef ARCH_X86_64
    movsxd        r2, r2d
%endif ; x86-64
%if %2 == 19
%if mmsize == 8 ; mmx
    mova          m2, [max_19bit_int]
%elifidn %5, sse4
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
    shl           r2, 1                  ; this allows *16 (i.e. now *8) in lea instructions for the 8-tap filter
%define r2shr 1
%else ; %3 == 4
%define r2shr 0
%endif ; %3 == 8
    lea           r4, [r4+r2*8]
%if %2 == 15
    lea           r1, [r1+r2*(2>>r2shr)]
%else ; %2 == 19
    lea           r1, [r1+r2*(4>>r2shr)]
%endif ; %2 == 15/19
    lea           r5, [r5+r2*(2>>r2shr)]
    neg           r2

.loop:
%if %3 == 4 ; filterSize == 4 scaling
    ; load 2x4 or 4x4 source pixels into m0/m1
    movsx         r0, word [r5+r2*2+0]   ; filterPos[0]
    movsx         r6, word [r5+r2*2+2]   ; filterPos[1]
    movlh         m0, [r3+r0*srcmul]     ; src[filterPos[0] + {0,1,2,3}]
%if mmsize == 8
    movlh         m1, [r3+r6*srcmul]     ; src[filterPos[1] + {0,1,2,3}]
%else ; mmsize == 16
%if %1 > 8
    movhps        m0, [r3+r6*srcmul]     ; src[filterPos[1] + {0,1,2,3}]
%else ; %1 == 8
    movd          m4, [r3+r6*srcmul]     ; src[filterPos[1] + {0,1,2,3}]
%endif
    movsx         r0, word [r5+r2*2+4]   ; filterPos[2]
    movsx         r6, word [r5+r2*2+6]   ; filterPos[3]
    movlh         m1, [r3+r0*srcmul]     ; src[filterPos[2] + {0,1,2,3}]
%if %1 > 8
    movhps        m1, [r3+r6*srcmul]     ; src[filterPos[3] + {0,1,2,3}]
%else ; %1 == 8
    movd          m5, [r3+r6*srcmul]     ; src[filterPos[3] + {0,1,2,3}]
    punpckldq     m0, m4
    punpckldq     m1, m5
%endif ; %1 == 8 && %5 <= ssse
%endif ; mmsize == 8/16
%if %1 == 8
    punpcklbw     m0, m3                 ; byte -> word
    punpcklbw     m1, m3                 ; byte -> word
%endif ; %1 == 8

    ; multiply with filter coefficients
%if %1 == 16 ; pmaddwd needs signed adds, so this moves unsigned -> signed, we'll
             ; add back 0x8000 * sum(coeffs) after the horizontal add
    psubw         m0, m6
    psubw         m1, m6
%endif ; %1 == 16
    pmaddwd       m0, [r4+r2*8+mmsize*0] ; *= filter[{0,1,..,6,7}]
    pmaddwd       m1, [r4+r2*8+mmsize*1] ; *= filter[{8,9,..,14,15}]

    ; add up horizontally (4 srcpix * 4 coefficients -> 1 dstpix)
%if mmsize == 8 ; mmx
    movq          m4, m0
    punpckldq     m0, m1
    punpckhdq     m4, m1
    paddd         m0, m4
%elifidn %5, sse2
    mova          m4, m0
    shufps        m0, m1, 10001000b
    shufps        m4, m1, 11011101b
    paddd         m0, m4
%else ; ssse3/sse4
    phaddd        m0, m1                 ; filter[{ 0, 1, 2, 3}]*src[filterPos[0]+{0,1,2,3}],
                                         ; filter[{ 4, 5, 6, 7}]*src[filterPos[1]+{0,1,2,3}],
                                         ; filter[{ 8, 9,10,11}]*src[filterPos[2]+{0,1,2,3}],
                                         ; filter[{12,13,14,15}]*src[filterPos[3]+{0,1,2,3}]
%endif ; mmx/sse2/ssse3/sse4
%else ; %3 == 8, i.e. filterSize == 8 scaling
    ; load 2x8 or 4x8 source pixels into m0, m1, m4 and m5
    movsx         r0, word [r5+r2*1+0]   ; filterPos[0]
    movsx         r6, word [r5+r2*1+2]   ; filterPos[1]
    movbh         m0, [r3+ r0   *srcmul] ; src[filterPos[0] + {0,1,2,3,4,5,6,7}]
%if mmsize == 8
    movbh         m1, [r3+(r0+4)*srcmul] ; src[filterPos[0] + {4,5,6,7}]
    movbh         m4, [r3+ r6   *srcmul] ; src[filterPos[1] + {0,1,2,3}]
    movbh         m5, [r3+(r6+4)*srcmul] ; src[filterPos[1] + {4,5,6,7}]
%else ; mmsize == 16
    movbh         m1, [r3+ r6   *srcmul] ; src[filterPos[1] + {0,1,2,3,4,5,6,7}]
    movsx         r0, word [r5+r2*1+4]   ; filterPos[2]
    movsx         r6, word [r5+r2*1+6]   ; filterPos[3]
    movbh         m4, [r3+ r0   *srcmul] ; src[filterPos[2] + {0,1,2,3,4,5,6,7}]
    movbh         m5, [r3+ r6   *srcmul] ; src[filterPos[3] + {0,1,2,3,4,5,6,7}]
%endif ; mmsize == 8/16
%if %1 == 8
    punpcklbw     m0, m3                 ; byte -> word
    punpcklbw     m1, m3                 ; byte -> word
    punpcklbw     m4, m3                 ; byte -> word
    punpcklbw     m5, m3                 ; byte -> word
%endif ; %1 == 8

    ; multiply
%if %1 == 16 ; pmaddwd needs signed adds, so this moves unsigned -> signed, we'll
             ; add back 0x8000 * sum(coeffs) after the horizontal add
    psubw         m0, m6
    psubw         m1, m6
    psubw         m4, m6
    psubw         m5, m6
%endif ; %1 == 16
    pmaddwd       m0, [r4+r2*8+mmsize*0] ; *= filter[{0,1,..,6,7}]
    pmaddwd       m1, [r4+r2*8+mmsize*1] ; *= filter[{8,9,..,14,15}]
    pmaddwd       m4, [r4+r2*8+mmsize*2] ; *= filter[{16,17,..,22,23}]
    pmaddwd       m5, [r4+r2*8+mmsize*3] ; *= filter[{24,25,..,30,31}]

    ; add up horizontally (8 srcpix * 8 coefficients -> 1 dstpix)
%if mmsize == 8
    paddd         m0, m1
    paddd         m4, m5
    movq          m1, m0
    punpckldq     m0, m4
    punpckhdq     m1, m4
    paddd         m0, m1
%elifidn %5, sse2
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
    phaddd        m0, m4                 ; filter[{ 0, 1,..., 6, 7}]*src[filterPos[0]+{0,1,...,6,7}],
                                         ; filter[{ 8, 9,...,14,15}]*src[filterPos[1]+{0,1,...,6,7}],
                                         ; filter[{16,17,...,22,23}]*src[filterPos[2]+{0,1,...,6,7}],
                                         ; filter[{24,25,...,30,31}]*src[filterPos[3]+{0,1,...,6,7}]
%endif ; mmx/sse2/ssse3/sse4
%endif ; %3 == 4/8

%else ; %3 == X, i.e. any filterSize scaling

%ifidn %4, X4
%define r6sub 4
%else ; %4 == X || %4 == X8
%define r6sub 0
%endif ; %4 ==/!= X4
%ifdef ARCH_X86_64
    push         r12
    movsxd        r6, r6d                ; filterSize
    lea          r12, [r3+(r6-r6sub)*srcmul] ; &src[filterSize&~4]
%define src_reg r11
%define r1x     r10
%define filter2 r12
%else ; x86-32
    lea           r0, [r3+(r6-r6sub)*srcmul] ; &src[filterSize&~4]
    mov          r6m, r0
%define src_reg r3
%define r1x     r1
%define filter2 r6m
%endif ; x86-32/64
    lea           r5, [r5+r2*2]
%if %2 == 15
    lea           r1, [r1+r2*2]
%else ; %2 == 19
    lea           r1, [r1+r2*4]
%endif ; %2 == 15/19
    movifnidn   r1mp, r1
    neg           r2

.loop:
    movsx         r0, word [r5+r2*2+0]   ; filterPos[0]
    movsx        r1x, word [r5+r2*2+2]   ; filterPos[1]
    ; FIXME maybe do 4px/iteration on x86-64 (x86-32 wouldn't have enough regs)?
    pxor          m4, m4
    pxor          m5, m5
    mov      src_reg, r3mp

.innerloop:
    ; load 2x4 (mmx) or 2x8 (sse) source pixels into m0/m1 -> m4/m5
    movbh         m0, [src_reg+r0 *srcmul]    ; src[filterPos[0] + {0,1,2,3(,4,5,6,7)}]
    movbh         m1, [src_reg+(r1x+r6sub)*srcmul]    ; src[filterPos[1] + {0,1,2,3(,4,5,6,7)}]
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
    pmaddwd       m0, [r4     ]          ; filter[{0,1,2,3(,4,5,6,7)}]
    pmaddwd       m1, [r4+(r6+r6sub)*2]          ; filter[filtersize+{0,1,2,3(,4,5,6,7)}]
    paddd         m4, m0
    paddd         m5, m1
    add           r4, mmsize
    add      src_reg, srcmul*mmsize/2
    cmp      src_reg, filter2            ; while (src += 4) < &src[filterSize]
    jl .innerloop

%ifidn %4, X4
    movsx        r1x, word [r5+r2*2+2]   ; filterPos[1]
    movlh         m0, [src_reg+r0 *srcmul] ; split last 4 srcpx of dstpx[0]
    sub          r1x, r6                   ; and first 4 srcpx of dstpx[1]
%if %1 > 8
    movhps        m0, [src_reg+(r1x+r6sub)*srcmul]
%else ; %1 == 8
    movd          m1, [src_reg+(r1x+r6sub)*srcmul]
    punpckldq     m0, m1
%endif ; %1 == 8 && %5 <= ssse
%if %1 == 8
    punpcklbw     m0, m3
%endif ; %1 == 8
%if %1 == 16 ; pmaddwd needs signed adds, so this moves unsigned -> signed, we'll
             ; add back 0x8000 * sum(coeffs) after the horizontal add
    psubw         m0, m6
%endif ; %1 == 16
    pmaddwd       m0, [r4]
%endif ; %4 == X4

    lea           r4, [r4+(r6+r6sub)*2]

%if mmsize == 8 ; mmx
    movq          m0, m4
    punpckldq     m4, m5
    punpckhdq     m0, m5
    paddd         m0, m4
%else ; mmsize == 16
%ifidn %5, sse2
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
%ifidn %5, sse2
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
    movifnidn     r1, r1mp
%endif ; %3 == X
%if %2 == 15
    packssdw      m0, m0
%ifnidn %3, X
    movh [r1+r2*(2>>r2shr)], m0
%else ; %3 == X
    movd   [r1+r2*2], m0
%endif ; %3 ==/!= X
%else ; %2 == 19
%if mmsize == 8
    PMINSD_MMX    m0, m2, m4
%elifidn %5, sse4
    pminsd        m0, m2
%else ; sse2/ssse3
    cvtdq2ps      m0, m0
    minps         m0, m2
    cvtps2dq      m0, m0
%endif ; mmx/sse2/ssse3/sse4
%ifnidn %3, X
    mova [r1+r2*(4>>r2shr)], m0
%else ; %3 == X
    movq   [r1+r2*4], m0
%endif ; %3 ==/!= X
%endif ; %2 == 15/19
%ifnidn %3, X
    add           r2, (mmsize<<r2shr)/4  ; both 8tap and 4tap really only do 4 pixels (or for mmx: 2 pixels)
                                         ; per iteration. see "shl r2,1" above as for why we do this
%else ; %3 == X
    add           r2, 2
%endif ; %3 ==/!= X
    jl .loop
%ifnidn %3, X
    REP_RET
%else ; %3 == X
%ifdef ARCH_X86_64
    pop          r12
    RET
%else ; x86-32
    REP_RET
%endif ; x86-32/64
%endif ; %3 ==/!= X
%endmacro

; SCALE_FUNCS source_width, intermediate_nbits, opt, n_xmm
%macro SCALE_FUNCS 4
SCALE_FUNC %1, %2, 4, 4,  %3, 6, %4
SCALE_FUNC %1, %2, 8, 8,  %3, 6, %4
%if mmsize == 8
SCALE_FUNC %1, %2, X, X,  %3, 7, %4
%else
SCALE_FUNC %1, %2, X, X4, %3, 7, %4
SCALE_FUNC %1, %2, X, X8, %3, 7, %4
%endif
%endmacro

; SCALE_FUNCS2 opt, 8_xmm_args, 9to10_xmm_args, 16_xmm_args
%macro SCALE_FUNCS2 4
%ifnidn %1, sse4
SCALE_FUNCS  8, 15, %1, %2
SCALE_FUNCS  9, 15, %1, %3
SCALE_FUNCS 10, 15, %1, %3
SCALE_FUNCS 14, 15, %1, %3
SCALE_FUNCS 16, 15, %1, %4
%endif ; !sse4
SCALE_FUNCS  8, 19, %1, %2
SCALE_FUNCS  9, 19, %1, %3
SCALE_FUNCS 10, 19, %1, %3
SCALE_FUNCS 14, 19, %1, %3
SCALE_FUNCS 16, 19, %1, %4
%endmacro

%ifdef ARCH_X86_32
INIT_MMX
SCALE_FUNCS2 mmx,   0, 0, 0
%endif
INIT_XMM
SCALE_FUNCS2 sse2,  6, 7, 8
SCALE_FUNCS2 ssse3, 6, 6, 8
SCALE_FUNCS2 sse4,  6, 6, 8

;-----------------------------------------------------------------------------
; vertical line scaling
;
; void yuv2plane1_<output_size>_<opt>(const int16_t *src, uint8_t *dst, int dstW,
;                                     const uint8_t *dither, int offset)
; and
; void yuv2planeX_<output_size>_<opt>(const int16_t *filter, int filterSize,
;                                     const int16_t **src, uint8_t *dst, int dstW,
;                                     const uint8_t *dither, int offset)
;
; Scale one or $filterSize lines of source data to generate one line of output
; data. The input is 15-bit in int16_t if $output_size is [8,10] and 19-bit in
; int32_t if $output_size is 16. $filter is 12-bits. $filterSize is a multiple
; of 2. $offset is either 0 or 3. $dither holds 8 values.
;-----------------------------------------------------------------------------

%macro yuv2planeX_fn 4

%ifdef ARCH_X86_32
%define cntr_reg r1
%define movsx mov
%else
%define cntr_reg r11
%define movsx movsxd
%endif

cglobal yuv2planeX_%2_%1, %4, 7, %3
%if %2 == 8 || %2 == 9 || %2 == 10
    pxor            m6,  m6
%endif ; %2 == 8/9/10

%if %2 == 8
%ifdef ARCH_X86_32
%assign pad 0x2c - (stack_offset & 15)
    SUB             rsp, pad
%define m_dith m7
%else ; x86-64
%define m_dith m9
%endif ; x86-32

    ; create registers holding dither
    movq        m_dith, [r5]             ; dither
    test            r6d, r6d
    jz              .no_rot
%if mmsize == 16
    punpcklqdq  m_dith,  m_dith
%endif ; mmsize == 16
    PALIGNR     m_dith,  m_dith,  3,  m0
.no_rot:
%if mmsize == 16
    punpcklbw   m_dith,  m6
%ifdef ARCH_X86_64
    punpcklwd       m8,  m_dith,  m6
    pslld           m8,  12
%else ; x86-32
    punpcklwd       m5,  m_dith,  m6
    pslld           m5,  12
%endif ; x86-32/64
    punpckhwd   m_dith,  m6
    pslld       m_dith,  12
%ifdef ARCH_X86_32
    mova      [rsp+ 0],  m5
    mova      [rsp+16],  m_dith
%endif
%else ; mmsize == 8
    punpcklbw       m5,  m_dith,  m6
    punpckhbw   m_dith,  m6
    punpcklwd       m4,  m5,  m6
    punpckhwd       m5,  m6
    punpcklwd       m3,  m_dith,  m6
    punpckhwd   m_dith,  m6
    pslld           m4,  12
    pslld           m5,  12
    pslld           m3,  12
    pslld       m_dith,  12
    mova      [rsp+ 0],  m4
    mova      [rsp+ 8],  m5
    mova      [rsp+16],  m3
    mova      [rsp+24],  m_dith
%endif ; mmsize == 8/16
%endif ; %2 == 8

    xor             r5,  r5

.pixelloop:
%assign %%i 0
    ; the rep here is for the 8bit output mmx case, where dither covers
    ; 8 pixels but we can only handle 2 pixels per register, and thus 4
    ; pixels per iteration. In order to not have to keep track of where
    ; we are w.r.t. dithering, we unroll the mmx/8bit loop x2.
%if %2 == 8
%rep 16/mmsize
%endif ; %2 == 8

%if %2 == 8
%ifdef ARCH_X86_32
    mova            m2, [rsp+mmsize*(0+%%i)]
    mova            m1, [rsp+mmsize*(1+%%i)]
%else ; x86-64
    mova            m2,  m8
    mova            m1,  m_dith
%endif ; x86-32/64
%else ; %2 == 9/10/16
    mova            m1, [yuv2yuvX_%2_start]
    mova            m2,  m1
%endif ; %2 == 8/9/10/16
    movsx     cntr_reg,  r1m
.filterloop_ %+ %%i:
    ; input pixels
    mov             r6, [r2+gprsize*cntr_reg-2*gprsize]
%if %2 == 16
    mova            m3, [r6+r5*4]
    mova            m5, [r6+r5*4+mmsize]
%else ; %2 == 8/9/10
    mova            m3, [r6+r5*2]
%endif ; %2 == 8/9/10/16
    mov             r6, [r2+gprsize*cntr_reg-gprsize]
%if %2 == 16
    mova            m4, [r6+r5*4]
    mova            m6, [r6+r5*4+mmsize]
%else ; %2 == 8/9/10
    mova            m4, [r6+r5*2]
%endif ; %2 == 8/9/10/16

    ; coefficients
    movd            m0, [r0+2*cntr_reg-4]; coeff[0], coeff[1]
%if %2 == 16
    pshuflw         m7,  m0,  0          ; coeff[0]
    pshuflw         m0,  m0,  0x55       ; coeff[1]
    pmovsxwd        m7,  m7              ; word -> dword
    pmovsxwd        m0,  m0              ; word -> dword

    pmulld          m3,  m7
    pmulld          m5,  m7
    pmulld          m4,  m0
    pmulld          m6,  m0

    paddd           m2,  m3
    paddd           m1,  m5
    paddd           m2,  m4
    paddd           m1,  m6
%else ; %2 == 10/9/8
    punpcklwd       m5,  m3,  m4
    punpckhwd       m3,  m4
    SPLATD          m0,  m0

    pmaddwd         m5,  m0
    pmaddwd         m3,  m0

    paddd           m2,  m5
    paddd           m1,  m3
%endif ; %2 == 8/9/10/16

    sub       cntr_reg,  2
    jg .filterloop_ %+ %%i

%if %2 == 16
    psrad           m2,  31 - %2
    psrad           m1,  31 - %2
%else ; %2 == 10/9/8
    psrad           m2,  27 - %2
    psrad           m1,  27 - %2
%endif ; %2 == 8/9/10/16

%if %2 == 8
    packssdw        m2,  m1
    packuswb        m2,  m2
    movh     [r3+r5*1],  m2
%else ; %2 == 9/10/16
%if %2 == 16
    packssdw        m2,  m1
    paddw           m2, [minshort]
%else ; %2 == 9/10
%ifidn %1, sse4
    packusdw        m2,  m1
%elifidn %1, avx
    packusdw        m2,  m1
%else ; mmx2/sse2
    packssdw        m2,  m1
    pmaxsw          m2,  m6
%endif ; mmx2/sse2/sse4/avx
    pminsw          m2, [yuv2yuvX_%2_upper]
%endif ; %2 == 9/10/16
    mova     [r3+r5*2],  m2
%endif ; %2 == 8/9/10/16

    add             r5,  mmsize/2
    sub             r4d, mmsize/2
%if %2 == 8
%assign %%i %%i+2
%endrep
%endif ; %2 == 8
    jg .pixelloop

%if %2 == 8
%ifdef ARCH_X86_32
    ADD             rsp, pad
    RET
%else ; x86-64
    REP_RET
%endif ; x86-32/64
%else ; %2 == 9/10/16
    REP_RET
%endif ; %2 == 8/9/10/16
%endmacro

%define PALIGNR PALIGNR_MMX
%ifdef ARCH_X86_32
INIT_MMX
yuv2planeX_fn mmx,   8,  0, 7
yuv2planeX_fn mmx2,  9,  0, 5
yuv2planeX_fn mmx2, 10,  0, 5
%endif

INIT_XMM
yuv2planeX_fn sse2,  8, 10, 7
yuv2planeX_fn sse2,  9,  7, 5
yuv2planeX_fn sse2, 10,  7, 5

%define PALIGNR PALIGNR_SSSE3
yuv2planeX_fn sse4,  8, 10, 7
yuv2planeX_fn sse4,  9,  7, 5
yuv2planeX_fn sse4, 10,  7, 5
yuv2planeX_fn sse4, 16,  8, 5

INIT_AVX
yuv2planeX_fn avx,   8, 10, 7
yuv2planeX_fn avx,   9,  7, 5
yuv2planeX_fn avx,  10,  7, 5

; %1=outout-bpc, %2=alignment (u/a)
%macro yuv2plane1_mainloop 2
.loop_%2:
%if %1 == 8
    paddsw          m0, m2, [r0+r2*2+mmsize*0]
    paddsw          m1, m3, [r0+r2*2+mmsize*1]
    psraw           m0, 7
    psraw           m1, 7
    packuswb        m0, m1
    mov%2      [r1+r2], m0
%elif %1 == 16
    paddd           m0, m4, [r0+r2*4+mmsize*0]
    paddd           m1, m4, [r0+r2*4+mmsize*1]
    paddd           m2, m4, [r0+r2*4+mmsize*2]
    paddd           m3, m4, [r0+r2*4+mmsize*3]
    psrad           m0, 3
    psrad           m1, 3
    psrad           m2, 3
    psrad           m3, 3
%if cpuflag(sse4) ; avx/sse4
    packusdw        m0, m1
    packusdw        m2, m3
%else ; mmx/sse2
    packssdw        m0, m1
    packssdw        m2, m3
    paddw           m0, m5
    paddw           m2, m5
%endif ; mmx/sse2/sse4/avx
    mov%2    [r1+r2*2], m0
    mov%2    [r1+r2*2+mmsize], m2
%else
    paddsw          m0, m2, [r0+r2*2+mmsize*0]
    paddsw          m1, m2, [r0+r2*2+mmsize*1]
    psraw           m0, 15 - %1
    psraw           m1, 15 - %1
    pmaxsw          m0, m4
    pmaxsw          m1, m4
    pminsw          m0, m3
    pminsw          m1, m3
    mov%2    [r1+r2*2], m0
    mov%2    [r1+r2*2+mmsize], m1
%endif
    add             r2, mmsize
    jl .loop_%2
%endmacro

%macro yuv2plane1_fn 3
cglobal yuv2plane1_%1, %3, %3, %2
    add             r2, mmsize - 1
    and             r2, ~(mmsize - 1)
%if %1 == 8
    add             r1, r2
%else ; %1 != 8
    lea             r1, [r1+r2*2]
%endif ; %1 == 8
%if %1 == 16
    lea             r0, [r0+r2*4]
%else ; %1 != 16
    lea             r0, [r0+r2*2]
%endif ; %1 == 16
    neg             r2

%if %1 == 8
    pxor            m4, m4               ; zero

    ; create registers holding dither
    movq            m3, [r3]             ; dither
    test           r4d, r4d
    jz              .no_rot
%if mmsize == 16
    punpcklqdq      m3, m3
%endif ; mmsize == 16
    PALIGNR_MMX     m3, m3, 3, m2
.no_rot:
%if mmsize == 8
    mova            m2, m3
    punpckhbw       m3, m4               ; byte->word
    punpcklbw       m2, m4               ; byte->word
%else
    punpcklbw       m3, m4
    mova            m2, m3
%endif
%elif %1 == 9
    pxor            m4, m4
    mova            m3, [pw_512]
    mova            m2, [pw_32]
%elif %1 == 10
    pxor            m4, m4
    mova            m3, [pw_1024]
    mova            m2, [pw_16]
%else ; %1 == 16
%if cpuflag(sse4) ; sse4/avx
    mova            m4, [pd_4]
%else ; mmx/sse2
    mova            m4, [pd_4min0x40000]
    mova            m5, [minshort]
%endif ; mmx/sse2/sse4/avx
%endif ; %1 == ..

    ; actual pixel scaling
%if mmsize == 8
    yuv2plane1_mainloop %1, a
%else ; mmsize == 16
    test            r1, 15
    jnz .unaligned
    yuv2plane1_mainloop %1, a
    REP_RET
.unaligned:
    yuv2plane1_mainloop %1, u
%endif ; mmsize == 8/16
    REP_RET
%endmacro

%ifdef ARCH_X86_32
INIT_MMX mmx
yuv2plane1_fn  8, 0, 5
yuv2plane1_fn 16, 0, 3

INIT_MMX mmx2
yuv2plane1_fn  9, 0, 3
yuv2plane1_fn 10, 0, 3
%endif

INIT_XMM sse2
yuv2plane1_fn  8, 5, 5
yuv2plane1_fn  9, 5, 3
yuv2plane1_fn 10, 5, 3
yuv2plane1_fn 16, 6, 3

INIT_XMM sse4
yuv2plane1_fn 16, 5, 3

INIT_XMM avx
yuv2plane1_fn  8, 5, 5
yuv2plane1_fn  9, 5, 3
yuv2plane1_fn 10, 5, 3
yuv2plane1_fn 16, 5, 3
