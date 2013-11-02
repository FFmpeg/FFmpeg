;******************************************************************************
;* VP8 MMXEXT optimizations
;* Copyright (c) 2010 Ronald S. Bultje <rsbultje@gmail.com>
;* Copyright (c) 2010 Jason Garrett-Glaser <darkshikari@gmail.com>
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

pw_27:    times 8 dw 27
pw_63:    times 8 dw 63

pb_4:     times 16 db 4
pb_F8:    times 16 db 0xF8
pb_FE:    times 16 db 0xFE
pb_27_63: times 8 db 27, 63
pb_18_63: times 8 db 18, 63
pb_9_63:  times 8 db  9, 63

cextern pb_1
cextern pb_3
cextern pw_9
cextern pw_18
cextern pb_80

SECTION .text

;-----------------------------------------------------------------------------
; void vp8_h/v_loop_filter_simple_<opt>(uint8_t *dst, int stride, int flim);
;-----------------------------------------------------------------------------

; macro called with 7 mm register indexes as argument, and 4 regular registers
;
; first 4 mm registers will carry the transposed pixel data
; the other three are scratchspace (one would be sufficient, but this allows
; for more spreading/pipelining and thus faster execution on OOE CPUs)
;
; first two regular registers are buf+4*stride and buf+5*stride
; third is -stride, fourth is +stride
%macro READ_8x4_INTERLEAVED 11
    ; interleave 8 (A-H) rows of 4 pixels each
    movd          m%1, [%8+%10*4]   ; A0-3
    movd          m%5, [%9+%10*4]   ; B0-3
    movd          m%2, [%8+%10*2]   ; C0-3
    movd          m%6, [%8+%10]     ; D0-3
    movd          m%3, [%8]         ; E0-3
    movd          m%7, [%9]         ; F0-3
    movd          m%4, [%9+%11]     ; G0-3
    punpcklbw     m%1, m%5          ; A/B interleaved
    movd          m%5, [%9+%11*2]   ; H0-3
    punpcklbw     m%2, m%6          ; C/D interleaved
    punpcklbw     m%3, m%7          ; E/F interleaved
    punpcklbw     m%4, m%5          ; G/H interleaved
%endmacro

; macro called with 7 mm register indexes as argument, and 5 regular registers
; first 11 mean the same as READ_8x4_TRANSPOSED above
; fifth regular register is scratchspace to reach the bottom 8 rows, it
; will be set to second regular register + 8*stride at the end
%macro READ_16x4_INTERLEAVED 12
    ; transpose 16 (A-P) rows of 4 pixels each
    lea           %12, [r0+8*r2]

    ; read (and interleave) those addressable by %8 (=r0), A/C/D/E/I/K/L/M
    movd          m%1, [%8+%10*4]   ; A0-3
    movd          m%3, [%12+%10*4]  ; I0-3
    movd          m%2, [%8+%10*2]   ; C0-3
    movd          m%4, [%12+%10*2]  ; K0-3
    movd          m%6, [%8+%10]     ; D0-3
    movd          m%5, [%12+%10]    ; L0-3
    movd          m%7, [%12]        ; M0-3
    add           %12, %11
    punpcklbw     m%1, m%3          ; A/I
    movd          m%3, [%8]         ; E0-3
    punpcklbw     m%2, m%4          ; C/K
    punpcklbw     m%6, m%5          ; D/L
    punpcklbw     m%3, m%7          ; E/M
    punpcklbw     m%2, m%6          ; C/D/K/L interleaved

    ; read (and interleave) those addressable by %9 (=r4), B/F/G/H/J/N/O/P
    movd         m%5, [%9+%10*4]   ; B0-3
    movd         m%4, [%12+%10*4]  ; J0-3
    movd         m%7, [%9]         ; F0-3
    movd         m%6, [%12]        ; N0-3
    punpcklbw    m%5, m%4          ; B/J
    punpcklbw    m%7, m%6          ; F/N
    punpcklbw    m%1, m%5          ; A/B/I/J interleaved
    punpcklbw    m%3, m%7          ; E/F/M/N interleaved
    movd         m%4, [%9+%11]     ; G0-3
    movd         m%6, [%12+%11]    ; O0-3
    movd         m%5, [%9+%11*2]   ; H0-3
    movd         m%7, [%12+%11*2]  ; P0-3
    punpcklbw    m%4, m%6          ; G/O
    punpcklbw    m%5, m%7          ; H/P
    punpcklbw    m%4, m%5          ; G/H/O/P interleaved
%endmacro

; write 4 mm registers of 2 dwords each
; first four arguments are mm register indexes containing source data
; last four are registers containing buf+4*stride, buf+5*stride,
; -stride and +stride
%macro WRITE_4x2D 8
    ; write out (2 dwords per register)
    movd    [%5+%7*4], m%1
    movd    [%5+%7*2], m%2
    movd         [%5], m%3
    movd      [%6+%8], m%4
    punpckhdq     m%1, m%1
    punpckhdq     m%2, m%2
    punpckhdq     m%3, m%3
    punpckhdq     m%4, m%4
    movd    [%6+%7*4], m%1
    movd      [%5+%7], m%2
    movd         [%6], m%3
    movd    [%6+%8*2], m%4
%endmacro

; write 4 xmm registers of 4 dwords each
; arguments same as WRITE_2x4D, but with an extra register, so that the 5 regular
; registers contain buf+4*stride, buf+5*stride, buf+12*stride, -stride and +stride
; we add 1*stride to the third regular registry in the process
; the 10th argument is 16 if it's a Y filter (i.e. all regular registers cover the
; same memory region), or 8 if they cover two separate buffers (third one points to
; a different memory region than the first two), allowing for more optimal code for
; the 16-width case
%macro WRITE_4x4D 10
    ; write out (4 dwords per register), start with dwords zero
    movd    [%5+%8*4], m%1
    movd         [%5], m%2
    movd    [%7+%8*4], m%3
    movd         [%7], m%4

    ; store dwords 1
    psrldq        m%1, 4
    psrldq        m%2, 4
    psrldq        m%3, 4
    psrldq        m%4, 4
    movd    [%6+%8*4], m%1
    movd         [%6], m%2
%if %10 == 16
    movd    [%6+%9*4], m%3
%endif
    movd      [%7+%9], m%4

    ; write dwords 2
    psrldq        m%1, 4
    psrldq        m%2, 4
%if %10 == 8
    movd    [%5+%8*2], m%1
    movd          %5d, m%3
%endif
    psrldq        m%3, 4
    psrldq        m%4, 4
%if %10 == 16
    movd    [%5+%8*2], m%1
%endif
    movd      [%6+%9], m%2
    movd    [%7+%8*2], m%3
    movd    [%7+%9*2], m%4
    add            %7, %9

    ; store dwords 3
    psrldq        m%1, 4
    psrldq        m%2, 4
    psrldq        m%3, 4
    psrldq        m%4, 4
%if %10 == 8
    mov     [%7+%8*4], %5d
    movd    [%6+%8*2], m%1
%else
    movd      [%5+%8], m%1
%endif
    movd    [%6+%9*2], m%2
    movd    [%7+%8*2], m%3
    movd    [%7+%9*2], m%4
%endmacro

; write 4 or 8 words in the mmx/xmm registers as 8 lines
; 1 and 2 are the registers to write, this can be the same (for SSE2)
; for pre-SSE4:
; 3 is a general-purpose register that we will clobber
; for SSE4:
; 3 is a pointer to the destination's 5th line
; 4 is a pointer to the destination's 4th line
; 5/6 is -stride and +stride
%macro WRITE_2x4W 6
    movd            %3d, %1
    punpckhdq        %1, %1
    mov       [%4+%5*4], %3w
    shr              %3, 16
    add              %4, %6
    mov       [%4+%5*4], %3w

    movd            %3d, %1
    add              %4, %5
    mov       [%4+%5*2], %3w
    shr              %3, 16
    mov       [%4+%5  ], %3w

    movd            %3d, %2
    punpckhdq        %2, %2
    mov       [%4     ], %3w
    shr              %3, 16
    mov       [%4+%6  ], %3w

    movd            %3d, %2
    add              %4, %6
    mov       [%4+%6  ], %3w
    shr              %3, 16
    mov       [%4+%6*2], %3w
    add              %4, %5
%endmacro

%macro WRITE_8W 5
%if cpuflag(sse4)
    pextrw    [%3+%4*4], %1, 0
    pextrw    [%2+%4*4], %1, 1
    pextrw    [%3+%4*2], %1, 2
    pextrw    [%3+%4  ], %1, 3
    pextrw    [%3     ], %1, 4
    pextrw    [%2     ], %1, 5
    pextrw    [%2+%5  ], %1, 6
    pextrw    [%2+%5*2], %1, 7
%else
    movd            %2d, %1
    psrldq           %1, 4
    mov       [%3+%4*4], %2w
    shr              %2, 16
    add              %3, %5
    mov       [%3+%4*4], %2w

    movd            %2d, %1
    psrldq           %1, 4
    add              %3, %4
    mov       [%3+%4*2], %2w
    shr              %2, 16
    mov       [%3+%4  ], %2w

    movd            %2d, %1
    psrldq           %1, 4
    mov       [%3     ], %2w
    shr              %2, 16
    mov       [%3+%5  ], %2w

    movd            %2d, %1
    add              %3, %5
    mov       [%3+%5  ], %2w
    shr              %2, 16
    mov       [%3+%5*2], %2w
%endif
%endmacro

%macro SIMPLE_LOOPFILTER 2
cglobal vp8_%1_loop_filter_simple, 3, %2, 8, dst, stride, flim, cntr
%if mmsize == 8 ; mmx/mmxext
    mov         cntrq, 2
%endif
%if cpuflag(ssse3)
    pxor           m0, m0
%endif
    SPLATB_REG     m7, flim, m0     ; splat "flim" into register

    ; set up indexes to address 4 rows
%if mmsize == 8
    DEFINE_ARGS dst1, mstride, stride, cntr, dst2
%else
    DEFINE_ARGS dst1, mstride, stride, dst3, dst2
%endif
    mov       strideq, mstrideq
    neg      mstrideq
%ifidn %1, h
    lea         dst1q, [dst1q+4*strideq-2]
%endif

%if mmsize == 8 ; mmx / mmxext
.next8px:
%endif
%ifidn %1, v
    ; read 4 half/full rows of pixels
    mova           m0, [dst1q+mstrideq*2]    ; p1
    mova           m1, [dst1q+mstrideq]      ; p0
    mova           m2, [dst1q]               ; q0
    mova           m3, [dst1q+ strideq]      ; q1
%else ; h
    lea         dst2q, [dst1q+ strideq]

%if mmsize == 8 ; mmx/mmxext
    READ_8x4_INTERLEAVED  0, 1, 2, 3, 4, 5, 6, dst1q, dst2q, mstrideq, strideq
%else ; sse2
    READ_16x4_INTERLEAVED 0, 1, 2, 3, 4, 5, 6, dst1q, dst2q, mstrideq, strideq, dst3q
%endif
    TRANSPOSE4x4W         0, 1, 2, 3, 4
%endif

    ; simple_limit
    mova           m5, m2           ; m5=backup of q0
    mova           m6, m1           ; m6=backup of p0
    psubusb        m1, m2           ; p0-q0
    psubusb        m2, m6           ; q0-p0
    por            m1, m2           ; FFABS(p0-q0)
    paddusb        m1, m1           ; m1=FFABS(p0-q0)*2

    mova           m4, m3
    mova           m2, m0
    psubusb        m3, m0           ; q1-p1
    psubusb        m0, m4           ; p1-q1
    por            m3, m0           ; FFABS(p1-q1)
    mova           m0, [pb_80]
    pxor           m2, m0
    pxor           m4, m0
    psubsb         m2, m4           ; m2=p1-q1 (signed) backup for below
    pand           m3, [pb_FE]
    psrlq          m3, 1            ; m3=FFABS(p1-q1)/2, this can be used signed
    paddusb        m3, m1
    psubusb        m3, m7
    pxor           m1, m1
    pcmpeqb        m3, m1           ; abs(p0-q0)*2+abs(p1-q1)/2<=flim mask(0xff/0x0)

    ; filter_common (use m2/p1-q1, m4=q0, m6=p0, m5/q0-p0 and m3/mask)
    mova           m4, m5
    pxor           m5, m0
    pxor           m0, m6
    psubsb         m5, m0           ; q0-p0 (signed)
    paddsb         m2, m5
    paddsb         m2, m5
    paddsb         m2, m5           ; a=(p1-q1) + 3*(q0-p0)
    pand           m2, m3           ; apply filter mask (m3)

    mova           m3, [pb_F8]
    mova           m1, m2
    paddsb         m2, [pb_4]       ; f1<<3=a+4
    paddsb         m1, [pb_3]       ; f2<<3=a+3
    pand           m2, m3
    pand           m1, m3           ; cache f2<<3

    pxor           m0, m0
    pxor           m3, m3
    pcmpgtb        m0, m2           ; which values are <0?
    psubb          m3, m2           ; -f1<<3
    psrlq          m2, 3            ; +f1
    psrlq          m3, 3            ; -f1
    pand           m3, m0
    pandn          m0, m2
    psubusb        m4, m0
    paddusb        m4, m3           ; q0-f1

    pxor           m0, m0
    pxor           m3, m3
    pcmpgtb        m0, m1           ; which values are <0?
    psubb          m3, m1           ; -f2<<3
    psrlq          m1, 3            ; +f2
    psrlq          m3, 3            ; -f2
    pand           m3, m0
    pandn          m0, m1
    paddusb        m6, m0
    psubusb        m6, m3           ; p0+f2

    ; store
%ifidn %1, v
    mova      [dst1q], m4
    mova [dst1q+mstrideq], m6
%else ; h
    inc        dst1q
    SBUTTERFLY    bw, 6, 4, 0

%if mmsize == 16 ; sse2
%if cpuflag(sse4)
    inc         dst2q
%endif
    WRITE_8W       m6, dst2q, dst1q, mstrideq, strideq
    lea         dst2q, [dst3q+mstrideq+1]
%if cpuflag(sse4)
    inc         dst3q
%endif
    WRITE_8W       m4, dst3q, dst2q, mstrideq, strideq
%else ; mmx/mmxext
    WRITE_2x4W     m6, m4, dst2q, dst1q, mstrideq, strideq
%endif
%endif

%if mmsize == 8 ; mmx/mmxext
    ; next 8 pixels
%ifidn %1, v
    add         dst1q, 8            ; advance 8 cols = pixels
%else ; h
    lea         dst1q, [dst1q+strideq*8-1]  ; advance 8 rows = lines
%endif
    dec         cntrq
    jg .next8px
    REP_RET
%else ; sse2
    RET
%endif
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
SIMPLE_LOOPFILTER v, 4
SIMPLE_LOOPFILTER h, 5
INIT_MMX mmxext
SIMPLE_LOOPFILTER v, 4
SIMPLE_LOOPFILTER h, 5
%endif

INIT_XMM sse2
SIMPLE_LOOPFILTER v, 3
SIMPLE_LOOPFILTER h, 5
INIT_XMM ssse3
SIMPLE_LOOPFILTER v, 3
SIMPLE_LOOPFILTER h, 5
INIT_XMM sse4
SIMPLE_LOOPFILTER h, 5

;-----------------------------------------------------------------------------
; void vp8_h/v_loop_filter<size>_inner_<opt>(uint8_t *dst, [uint8_t *v,] int stride,
;                                            int flimE, int flimI, int hev_thr);
;-----------------------------------------------------------------------------

%macro INNER_LOOPFILTER 2
%define stack_size 0
%ifndef m8   ; stack layout: [0]=E, [1]=I, [2]=hev_thr
%ifidn %1, v ;               [3]=hev() result
%define stack_size mmsize * -4
%else ; h    ; extra storage space for transposes
%define stack_size mmsize * -5
%endif
%endif

%if %2 == 8 ; chroma
cglobal vp8_%1_loop_filter8uv_inner, 6, 6, 13, stack_size, dst, dst8, stride, flimE, flimI, hevthr
%else ; luma
cglobal vp8_%1_loop_filter16y_inner, 5, 5, 13, stack_size, dst, stride, flimE, flimI, hevthr
%endif

%if cpuflag(ssse3)
    pxor             m7, m7
%endif

%ifndef m8
    ; splat function arguments
    SPLATB_REG       m0, flimEq, m7   ; E
    SPLATB_REG       m1, flimIq, m7   ; I
    SPLATB_REG       m2, hevthrq, m7  ; hev_thresh

%define m_flimE    [rsp]
%define m_flimI    [rsp+mmsize]
%define m_hevthr   [rsp+mmsize*2]
%define m_maskres  [rsp+mmsize*3]
%define m_p0backup [rsp+mmsize*3]
%define m_q0backup [rsp+mmsize*4]

    mova        m_flimE, m0
    mova        m_flimI, m1
    mova       m_hevthr, m2
%else
%define m_flimE    m9
%define m_flimI    m10
%define m_hevthr   m11
%define m_maskres  m12
%define m_p0backup m12
%define m_q0backup m8

    ; splat function arguments
    SPLATB_REG  m_flimE, flimEq, m7   ; E
    SPLATB_REG  m_flimI, flimIq, m7   ; I
    SPLATB_REG m_hevthr, hevthrq, m7  ; hev_thresh
%endif

%if %2 == 8 ; chroma
    DEFINE_ARGS dst1, dst8, mstride, stride, dst2
%elif mmsize == 8
    DEFINE_ARGS dst1, mstride, stride, dst2, cntr
    mov           cntrq, 2
%else
    DEFINE_ARGS dst1, mstride, stride, dst2, dst8
%endif
    mov         strideq, mstrideq
    neg        mstrideq
%ifidn %1, h
    lea           dst1q, [dst1q+strideq*4-4]
%if %2 == 8 ; chroma
    lea           dst8q, [dst8q+strideq*4-4]
%endif
%endif

%if mmsize == 8
.next8px:
%endif
    ; read
    lea           dst2q, [dst1q+strideq]
%ifidn %1, v
%if %2 == 8 && mmsize == 16
%define movrow movh
%else
%define movrow mova
%endif
    movrow           m0, [dst1q+mstrideq*4] ; p3
    movrow           m1, [dst2q+mstrideq*4] ; p2
    movrow           m2, [dst1q+mstrideq*2] ; p1
    movrow           m5, [dst2q]            ; q1
    movrow           m6, [dst2q+ strideq*1] ; q2
    movrow           m7, [dst2q+ strideq*2] ; q3
%if mmsize == 16 && %2 == 8
    movhps           m0, [dst8q+mstrideq*4]
    movhps           m2, [dst8q+mstrideq*2]
    add           dst8q, strideq
    movhps           m1, [dst8q+mstrideq*4]
    movhps           m5, [dst8q]
    movhps           m6, [dst8q+ strideq  ]
    movhps           m7, [dst8q+ strideq*2]
    add           dst8q, mstrideq
%endif
%elif mmsize == 8 ; mmx/mmxext (h)
    ; read 8 rows of 8px each
    movu             m0, [dst1q+mstrideq*4]
    movu             m1, [dst2q+mstrideq*4]
    movu             m2, [dst1q+mstrideq*2]
    movu             m3, [dst1q+mstrideq  ]
    movu             m4, [dst1q]
    movu             m5, [dst2q]
    movu             m6, [dst2q+ strideq  ]

    ; 8x8 transpose
    TRANSPOSE4x4B     0, 1, 2, 3, 7
    mova     m_q0backup, m1
    movu             m7, [dst2q+ strideq*2]
    TRANSPOSE4x4B     4, 5, 6, 7, 1
    SBUTTERFLY       dq, 0, 4, 1     ; p3/p2
    SBUTTERFLY       dq, 2, 6, 1     ; q0/q1
    SBUTTERFLY       dq, 3, 7, 1     ; q2/q3
    mova             m1, m_q0backup
    mova     m_q0backup, m2          ; store q0
    SBUTTERFLY       dq, 1, 5, 2     ; p1/p0
    mova     m_p0backup, m5          ; store p0
    SWAP              1, 4
    SWAP              2, 4
    SWAP              6, 3
    SWAP              5, 3
%else ; sse2 (h)
%if %2 == 16
    lea           dst8q, [dst1q+ strideq*8]
%endif

    ; read 16 rows of 8px each, interleave
    movh             m0, [dst1q+mstrideq*4]
    movh             m1, [dst8q+mstrideq*4]
    movh             m2, [dst1q+mstrideq*2]
    movh             m5, [dst8q+mstrideq*2]
    movh             m3, [dst1q+mstrideq  ]
    movh             m6, [dst8q+mstrideq  ]
    movh             m4, [dst1q]
    movh             m7, [dst8q]
    punpcklbw        m0, m1          ; A/I
    punpcklbw        m2, m5          ; C/K
    punpcklbw        m3, m6          ; D/L
    punpcklbw        m4, m7          ; E/M

    add           dst8q, strideq
    movh             m1, [dst2q+mstrideq*4]
    movh             m6, [dst8q+mstrideq*4]
    movh             m5, [dst2q]
    movh             m7, [dst8q]
    punpcklbw        m1, m6          ; B/J
    punpcklbw        m5, m7          ; F/N
    movh             m6, [dst2q+ strideq  ]
    movh             m7, [dst8q+ strideq  ]
    punpcklbw        m6, m7          ; G/O

    ; 8x16 transpose
    TRANSPOSE4x4B     0, 1, 2, 3, 7
%ifdef m8
    SWAP              1, 8
%else
    mova     m_q0backup, m1
%endif
    movh             m7, [dst2q+ strideq*2]
    movh             m1, [dst8q+ strideq*2]
    punpcklbw        m7, m1          ; H/P
    TRANSPOSE4x4B     4, 5, 6, 7, 1
    SBUTTERFLY       dq, 0, 4, 1     ; p3/p2
    SBUTTERFLY       dq, 2, 6, 1     ; q0/q1
    SBUTTERFLY       dq, 3, 7, 1     ; q2/q3
%ifdef m8
    SWAP              1, 8
    SWAP              2, 8
%else
    mova             m1, m_q0backup
    mova     m_q0backup, m2          ; store q0
%endif
    SBUTTERFLY       dq, 1, 5, 2     ; p1/p0
%ifdef m12
    SWAP              5, 12
%else
    mova     m_p0backup, m5          ; store p0
%endif
    SWAP              1, 4
    SWAP              2, 4
    SWAP              6, 3
    SWAP              5, 3
%endif

    ; normal_limit for p3-p2, p2-p1, q3-q2 and q2-q1
    mova             m4, m1
    SWAP              4, 1
    psubusb          m4, m0          ; p2-p3
    psubusb          m0, m1          ; p3-p2
    por              m0, m4          ; abs(p3-p2)

    mova             m4, m2
    SWAP              4, 2
    psubusb          m4, m1          ; p1-p2
    psubusb          m1, m2          ; p2-p1
    por              m1, m4          ; abs(p2-p1)

    mova             m4, m6
    SWAP              4, 6
    psubusb          m4, m7          ; q2-q3
    psubusb          m7, m6          ; q3-q2
    por              m7, m4          ; abs(q3-q2)

    mova             m4, m5
    SWAP              4, 5
    psubusb          m4, m6          ; q1-q2
    psubusb          m6, m5          ; q2-q1
    por              m6, m4          ; abs(q2-q1)

%if notcpuflag(mmxext)
    mova             m4, m_flimI
    pxor             m3, m3
    psubusb          m0, m4
    psubusb          m1, m4
    psubusb          m7, m4
    psubusb          m6, m4
    pcmpeqb          m0, m3          ; abs(p3-p2) <= I
    pcmpeqb          m1, m3          ; abs(p2-p1) <= I
    pcmpeqb          m7, m3          ; abs(q3-q2) <= I
    pcmpeqb          m6, m3          ; abs(q2-q1) <= I
    pand             m0, m1
    pand             m7, m6
    pand             m0, m7
%else ; mmxext/sse2
    pmaxub           m0, m1
    pmaxub           m6, m7
    pmaxub           m0, m6
%endif

    ; normal_limit and high_edge_variance for p1-p0, q1-q0
    SWAP              7, 3           ; now m7 is zero
%ifidn %1, v
    movrow           m3, [dst1q+mstrideq  ] ; p0
%if mmsize == 16 && %2 == 8
    movhps           m3, [dst8q+mstrideq  ]
%endif
%elifdef m12
    SWAP              3, 12
%else
    mova             m3, m_p0backup
%endif

    mova             m1, m2
    SWAP              1, 2
    mova             m6, m3
    SWAP              3, 6
    psubusb          m1, m3          ; p1-p0
    psubusb          m6, m2          ; p0-p1
    por              m1, m6          ; abs(p1-p0)
%if notcpuflag(mmxext)
    mova             m6, m1
    psubusb          m1, m4
    psubusb          m6, m_hevthr
    pcmpeqb          m1, m7          ; abs(p1-p0) <= I
    pcmpeqb          m6, m7          ; abs(p1-p0) <= hev_thresh
    pand             m0, m1
    mova      m_maskres, m6
%else ; mmxext/sse2
    pmaxub           m0, m1          ; max_I
    SWAP              1, 4           ; max_hev_thresh
%endif

    SWAP              6, 4           ; now m6 is I
%ifidn %1, v
    movrow           m4, [dst1q]     ; q0
%if mmsize == 16 && %2 == 8
    movhps           m4, [dst8q]
%endif
%elifdef m8
    SWAP              4, 8
%else
    mova             m4, m_q0backup
%endif
    mova             m1, m4
    SWAP              1, 4
    mova             m7, m5
    SWAP              7, 5
    psubusb          m1, m5          ; q0-q1
    psubusb          m7, m4          ; q1-q0
    por              m1, m7          ; abs(q1-q0)
%if notcpuflag(mmxext)
    mova             m7, m1
    psubusb          m1, m6
    psubusb          m7, m_hevthr
    pxor             m6, m6
    pcmpeqb          m1, m6          ; abs(q1-q0) <= I
    pcmpeqb          m7, m6          ; abs(q1-q0) <= hev_thresh
    mova             m6, m_maskres
    pand             m0, m1          ; abs([pq][321]-[pq][210]) <= I
    pand             m6, m7
%else ; mmxext/sse2
    pxor             m7, m7
    pmaxub           m0, m1
    pmaxub           m6, m1
    psubusb          m0, m_flimI
    psubusb          m6, m_hevthr
    pcmpeqb          m0, m7          ; max(abs(..)) <= I
    pcmpeqb          m6, m7          ; !(max(abs..) > thresh)
%endif
%ifdef m12
    SWAP              6, 12
%else
    mova      m_maskres, m6          ; !(abs(p1-p0) > hev_t || abs(q1-q0) > hev_t)
%endif

    ; simple_limit
    mova             m1, m3
    SWAP              1, 3
    mova             m6, m4          ; keep copies of p0/q0 around for later use
    SWAP              6, 4
    psubusb          m1, m4          ; p0-q0
    psubusb          m6, m3          ; q0-p0
    por              m1, m6          ; abs(q0-p0)
    paddusb          m1, m1          ; m1=2*abs(q0-p0)

    mova             m7, m2
    SWAP              7, 2
    mova             m6, m5
    SWAP              6, 5
    psubusb          m7, m5          ; p1-q1
    psubusb          m6, m2          ; q1-p1
    por              m7, m6          ; abs(q1-p1)
    pxor             m6, m6
    pand             m7, [pb_FE]
    psrlq            m7, 1           ; abs(q1-p1)/2
    paddusb          m7, m1          ; abs(q0-p0)*2+abs(q1-p1)/2
    psubusb          m7, m_flimE
    pcmpeqb          m7, m6          ; abs(q0-p0)*2+abs(q1-p1)/2 <= E
    pand             m0, m7          ; normal_limit result

    ; filter_common; at this point, m2-m5=p1-q1 and m0 is filter_mask
%ifdef m8 ; x86-64 && sse2
    mova             m8, [pb_80]
%define m_pb_80 m8
%else ; x86-32 or mmx/mmxext
%define m_pb_80 [pb_80]
%endif
    mova             m1, m4
    mova             m7, m3
    pxor             m1, m_pb_80
    pxor             m7, m_pb_80
    psubsb           m1, m7          ; (signed) q0-p0
    mova             m6, m2
    mova             m7, m5
    pxor             m6, m_pb_80
    pxor             m7, m_pb_80
    psubsb           m6, m7          ; (signed) p1-q1
    mova             m7, m_maskres
    pandn            m7, m6
    paddsb           m7, m1
    paddsb           m7, m1
    paddsb           m7, m1          ; 3*(q0-p0)+is4tap?(p1-q1)

    pand             m7, m0
    mova             m1, [pb_F8]
    mova             m6, m7
    paddsb           m7, [pb_3]
    paddsb           m6, [pb_4]
    pand             m7, m1
    pand             m6, m1

    pxor             m1, m1
    pxor             m0, m0
    pcmpgtb          m1, m7
    psubb            m0, m7
    psrlq            m7, 3           ; +f2
    psrlq            m0, 3           ; -f2
    pand             m0, m1
    pandn            m1, m7
    psubusb          m3, m0
    paddusb          m3, m1          ; p0+f2

    pxor             m1, m1
    pxor             m0, m0
    pcmpgtb          m0, m6
    psubb            m1, m6
    psrlq            m6, 3           ; +f1
    psrlq            m1, 3           ; -f1
    pand             m1, m0
    pandn            m0, m6
    psubusb          m4, m0
    paddusb          m4, m1          ; q0-f1

%ifdef m12
    SWAP              6, 12
%else
    mova             m6, m_maskres
%endif
%if notcpuflag(mmxext)
    mova             m7, [pb_1]
%else ; mmxext/sse2
    pxor             m7, m7
%endif
    pand             m0, m6
    pand             m1, m6
%if notcpuflag(mmxext)
    paddusb          m0, m7
    pand             m1, [pb_FE]
    pandn            m7, m0
    psrlq            m1, 1
    psrlq            m7, 1
    SWAP              0, 7
%else ; mmxext/sse2
    psubusb          m1, [pb_1]
    pavgb            m0, m7          ; a
    pavgb            m1, m7          ; -a
%endif
    psubusb          m5, m0
    psubusb          m2, m1
    paddusb          m5, m1          ; q1-a
    paddusb          m2, m0          ; p1+a

    ; store
%ifidn %1, v
    movrow [dst1q+mstrideq*2], m2
    movrow [dst1q+mstrideq  ], m3
    movrow      [dst1q], m4
    movrow [dst1q+ strideq  ], m5
%if mmsize == 16 && %2 == 8
    movhps [dst8q+mstrideq*2], m2
    movhps [dst8q+mstrideq  ], m3
    movhps      [dst8q], m4
    movhps [dst8q+ strideq  ], m5
%endif
%else ; h
    add           dst1q, 2
    add           dst2q, 2

    ; 4x8/16 transpose
    TRANSPOSE4x4B     2, 3, 4, 5, 6

%if mmsize == 8 ; mmx/mmxext (h)
    WRITE_4x2D        2, 3, 4, 5, dst1q, dst2q, mstrideq, strideq
%else ; sse2 (h)
    lea           dst8q, [dst8q+mstrideq  +2]
    WRITE_4x4D        2, 3, 4, 5, dst1q, dst2q, dst8q, mstrideq, strideq, %2
%endif
%endif

%if mmsize == 8
%if %2 == 8 ; chroma
%ifidn %1, h
    sub           dst1q, 2
%endif
    cmp           dst1q, dst8q
    mov           dst1q, dst8q
    jnz .next8px
%else
%ifidn %1, h
    lea           dst1q, [dst1q+ strideq*8-2]
%else ; v
    add           dst1q, 8
%endif
    dec           cntrq
    jg .next8px
%endif
    REP_RET
%else ; mmsize == 16
    RET
%endif
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
INNER_LOOPFILTER v, 16
INNER_LOOPFILTER h, 16
INNER_LOOPFILTER v,  8
INNER_LOOPFILTER h,  8

INIT_MMX mmxext
INNER_LOOPFILTER v, 16
INNER_LOOPFILTER h, 16
INNER_LOOPFILTER v,  8
INNER_LOOPFILTER h,  8
%endif

INIT_XMM sse2
INNER_LOOPFILTER v, 16
INNER_LOOPFILTER h, 16
INNER_LOOPFILTER v,  8
INNER_LOOPFILTER h,  8

INIT_XMM ssse3
INNER_LOOPFILTER v, 16
INNER_LOOPFILTER h, 16
INNER_LOOPFILTER v,  8
INNER_LOOPFILTER h,  8

;-----------------------------------------------------------------------------
; void vp8_h/v_loop_filter<size>_mbedge_<opt>(uint8_t *dst, [uint8_t *v,] int stride,
;                                            int flimE, int flimI, int hev_thr);
;-----------------------------------------------------------------------------

%macro MBEDGE_LOOPFILTER 2
%define stack_size 0
%ifndef m8       ; stack layout: [0]=E, [1]=I, [2]=hev_thr
%if mmsize == 16 ;               [3]=hev() result
                 ;               [4]=filter tmp result
                 ;               [5]/[6] = p2/q2 backup
                 ;               [7]=lim_res sign result
%define stack_size mmsize * -7
%else ; 8        ; extra storage space for transposes
%define stack_size mmsize * -8
%endif
%endif

%if %2 == 8 ; chroma
cglobal vp8_%1_loop_filter8uv_mbedge, 6, 6, 15, stack_size, dst1, dst8, stride, flimE, flimI, hevthr
%else ; luma
cglobal vp8_%1_loop_filter16y_mbedge, 5, 5, 15, stack_size, dst1, stride, flimE, flimI, hevthr
%endif

%if cpuflag(ssse3)
    pxor             m7, m7
%endif

%ifndef m8
    ; splat function arguments
    SPLATB_REG       m0, flimEq, m7   ; E
    SPLATB_REG       m1, flimIq, m7   ; I
    SPLATB_REG       m2, hevthrq, m7  ; hev_thresh

%define m_flimE    [rsp]
%define m_flimI    [rsp+mmsize]
%define m_hevthr   [rsp+mmsize*2]
%define m_maskres  [rsp+mmsize*3]
%define m_limres   [rsp+mmsize*4]
%define m_p0backup [rsp+mmsize*3]
%define m_q0backup [rsp+mmsize*4]
%define m_p2backup [rsp+mmsize*5]
%define m_q2backup [rsp+mmsize*6]
%if mmsize == 16
%define m_limsign  [rsp]
%else
%define m_limsign  [rsp+mmsize*7]
%endif

    mova        m_flimE, m0
    mova        m_flimI, m1
    mova       m_hevthr, m2
%else ; sse2 on x86-64
%define m_flimE    m9
%define m_flimI    m10
%define m_hevthr   m11
%define m_maskres  m12
%define m_limres   m8
%define m_p0backup m12
%define m_q0backup m8
%define m_p2backup m13
%define m_q2backup m14
%define m_limsign  m9

    ; splat function arguments
    SPLATB_REG  m_flimE, flimEq, m7   ; E
    SPLATB_REG  m_flimI, flimIq, m7   ; I
    SPLATB_REG m_hevthr, hevthrq, m7  ; hev_thresh
%endif

%if %2 == 8 ; chroma
    DEFINE_ARGS dst1, dst8, mstride, stride, dst2
%elif mmsize == 8
    DEFINE_ARGS dst1, mstride, stride, dst2, cntr
    mov           cntrq, 2
%else
    DEFINE_ARGS dst1, mstride, stride, dst2, dst8
%endif
    mov         strideq, mstrideq
    neg        mstrideq
%ifidn %1, h
    lea           dst1q, [dst1q+strideq*4-4]
%if %2 == 8 ; chroma
    lea           dst8q, [dst8q+strideq*4-4]
%endif
%endif

%if mmsize == 8
.next8px:
%endif
    ; read
    lea           dst2q, [dst1q+ strideq  ]
%ifidn %1, v
%if %2 == 8 && mmsize == 16
%define movrow movh
%else
%define movrow mova
%endif
    movrow           m0, [dst1q+mstrideq*4] ; p3
    movrow           m1, [dst2q+mstrideq*4] ; p2
    movrow           m2, [dst1q+mstrideq*2] ; p1
    movrow           m5, [dst2q]            ; q1
    movrow           m6, [dst2q+ strideq  ] ; q2
    movrow           m7, [dst2q+ strideq*2] ; q3
%if mmsize == 16 && %2 == 8
    movhps           m0, [dst8q+mstrideq*4]
    movhps           m2, [dst8q+mstrideq*2]
    add           dst8q, strideq
    movhps           m1, [dst8q+mstrideq*4]
    movhps           m5, [dst8q]
    movhps           m6, [dst8q+ strideq  ]
    movhps           m7, [dst8q+ strideq*2]
    add           dst8q, mstrideq
%endif
%elif mmsize == 8 ; mmx/mmxext (h)
    ; read 8 rows of 8px each
    movu             m0, [dst1q+mstrideq*4]
    movu             m1, [dst2q+mstrideq*4]
    movu             m2, [dst1q+mstrideq*2]
    movu             m3, [dst1q+mstrideq  ]
    movu             m4, [dst1q]
    movu             m5, [dst2q]
    movu             m6, [dst2q+ strideq  ]

    ; 8x8 transpose
    TRANSPOSE4x4B     0, 1, 2, 3, 7
    mova     m_q0backup, m1
    movu             m7, [dst2q+ strideq*2]
    TRANSPOSE4x4B     4, 5, 6, 7, 1
    SBUTTERFLY       dq, 0, 4, 1     ; p3/p2
    SBUTTERFLY       dq, 2, 6, 1     ; q0/q1
    SBUTTERFLY       dq, 3, 7, 1     ; q2/q3
    mova             m1, m_q0backup
    mova     m_q0backup, m2          ; store q0
    SBUTTERFLY       dq, 1, 5, 2     ; p1/p0
    mova     m_p0backup, m5          ; store p0
    SWAP              1, 4
    SWAP              2, 4
    SWAP              6, 3
    SWAP              5, 3
%else ; sse2 (h)
%if %2 == 16
    lea           dst8q, [dst1q+ strideq*8  ]
%endif

    ; read 16 rows of 8px each, interleave
    movh             m0, [dst1q+mstrideq*4]
    movh             m1, [dst8q+mstrideq*4]
    movh             m2, [dst1q+mstrideq*2]
    movh             m5, [dst8q+mstrideq*2]
    movh             m3, [dst1q+mstrideq  ]
    movh             m6, [dst8q+mstrideq  ]
    movh             m4, [dst1q]
    movh             m7, [dst8q]
    punpcklbw        m0, m1          ; A/I
    punpcklbw        m2, m5          ; C/K
    punpcklbw        m3, m6          ; D/L
    punpcklbw        m4, m7          ; E/M

    add           dst8q, strideq
    movh             m1, [dst2q+mstrideq*4]
    movh             m6, [dst8q+mstrideq*4]
    movh             m5, [dst2q]
    movh             m7, [dst8q]
    punpcklbw        m1, m6          ; B/J
    punpcklbw        m5, m7          ; F/N
    movh             m6, [dst2q+ strideq  ]
    movh             m7, [dst8q+ strideq  ]
    punpcklbw        m6, m7          ; G/O

    ; 8x16 transpose
    TRANSPOSE4x4B     0, 1, 2, 3, 7
%ifdef m8
    SWAP              1, 8
%else
    mova     m_q0backup, m1
%endif
    movh             m7, [dst2q+ strideq*2]
    movh             m1, [dst8q+ strideq*2]
    punpcklbw        m7, m1          ; H/P
    TRANSPOSE4x4B     4, 5, 6, 7, 1
    SBUTTERFLY       dq, 0, 4, 1     ; p3/p2
    SBUTTERFLY       dq, 2, 6, 1     ; q0/q1
    SBUTTERFLY       dq, 3, 7, 1     ; q2/q3
%ifdef m8
    SWAP              1, 8
    SWAP              2, 8
%else
    mova             m1, m_q0backup
    mova     m_q0backup, m2          ; store q0
%endif
    SBUTTERFLY       dq, 1, 5, 2     ; p1/p0
%ifdef m12
    SWAP              5, 12
%else
    mova     m_p0backup, m5          ; store p0
%endif
    SWAP              1, 4
    SWAP              2, 4
    SWAP              6, 3
    SWAP              5, 3
%endif

    ; normal_limit for p3-p2, p2-p1, q3-q2 and q2-q1
    mova             m4, m1
    SWAP              4, 1
    psubusb          m4, m0          ; p2-p3
    psubusb          m0, m1          ; p3-p2
    por              m0, m4          ; abs(p3-p2)

    mova             m4, m2
    SWAP              4, 2
    psubusb          m4, m1          ; p1-p2
    mova     m_p2backup, m1
    psubusb          m1, m2          ; p2-p1
    por              m1, m4          ; abs(p2-p1)

    mova             m4, m6
    SWAP              4, 6
    psubusb          m4, m7          ; q2-q3
    psubusb          m7, m6          ; q3-q2
    por              m7, m4          ; abs(q3-q2)

    mova             m4, m5
    SWAP              4, 5
    psubusb          m4, m6          ; q1-q2
    mova     m_q2backup, m6
    psubusb          m6, m5          ; q2-q1
    por              m6, m4          ; abs(q2-q1)

%if notcpuflag(mmxext)
    mova             m4, m_flimI
    pxor             m3, m3
    psubusb          m0, m4
    psubusb          m1, m4
    psubusb          m7, m4
    psubusb          m6, m4
    pcmpeqb          m0, m3          ; abs(p3-p2) <= I
    pcmpeqb          m1, m3          ; abs(p2-p1) <= I
    pcmpeqb          m7, m3          ; abs(q3-q2) <= I
    pcmpeqb          m6, m3          ; abs(q2-q1) <= I
    pand             m0, m1
    pand             m7, m6
    pand             m0, m7
%else ; mmxext/sse2
    pmaxub           m0, m1
    pmaxub           m6, m7
    pmaxub           m0, m6
%endif

    ; normal_limit and high_edge_variance for p1-p0, q1-q0
    SWAP              7, 3           ; now m7 is zero
%ifidn %1, v
    movrow           m3, [dst1q+mstrideq  ] ; p0
%if mmsize == 16 && %2 == 8
    movhps           m3, [dst8q+mstrideq  ]
%endif
%elifdef m12
    SWAP              3, 12
%else
    mova             m3, m_p0backup
%endif

    mova             m1, m2
    SWAP              1, 2
    mova             m6, m3
    SWAP              3, 6
    psubusb          m1, m3          ; p1-p0
    psubusb          m6, m2          ; p0-p1
    por              m1, m6          ; abs(p1-p0)
%if notcpuflag(mmxext)
    mova             m6, m1
    psubusb          m1, m4
    psubusb          m6, m_hevthr
    pcmpeqb          m1, m7          ; abs(p1-p0) <= I
    pcmpeqb          m6, m7          ; abs(p1-p0) <= hev_thresh
    pand             m0, m1
    mova      m_maskres, m6
%else ; mmxext/sse2
    pmaxub           m0, m1          ; max_I
    SWAP              1, 4           ; max_hev_thresh
%endif

    SWAP              6, 4           ; now m6 is I
%ifidn %1, v
    movrow           m4, [dst1q]     ; q0
%if mmsize == 16 && %2 == 8
    movhps           m4, [dst8q]
%endif
%elifdef m8
    SWAP              4, 8
%else
    mova             m4, m_q0backup
%endif
    mova             m1, m4
    SWAP              1, 4
    mova             m7, m5
    SWAP              7, 5
    psubusb          m1, m5          ; q0-q1
    psubusb          m7, m4          ; q1-q0
    por              m1, m7          ; abs(q1-q0)
%if notcpuflag(mmxext)
    mova             m7, m1
    psubusb          m1, m6
    psubusb          m7, m_hevthr
    pxor             m6, m6
    pcmpeqb          m1, m6          ; abs(q1-q0) <= I
    pcmpeqb          m7, m6          ; abs(q1-q0) <= hev_thresh
    mova             m6, m_maskres
    pand             m0, m1          ; abs([pq][321]-[pq][210]) <= I
    pand             m6, m7
%else ; mmxext/sse2
    pxor             m7, m7
    pmaxub           m0, m1
    pmaxub           m6, m1
    psubusb          m0, m_flimI
    psubusb          m6, m_hevthr
    pcmpeqb          m0, m7          ; max(abs(..)) <= I
    pcmpeqb          m6, m7          ; !(max(abs..) > thresh)
%endif
%ifdef m12
    SWAP              6, 12
%else
    mova      m_maskres, m6          ; !(abs(p1-p0) > hev_t || abs(q1-q0) > hev_t)
%endif

    ; simple_limit
    mova             m1, m3
    SWAP              1, 3
    mova             m6, m4          ; keep copies of p0/q0 around for later use
    SWAP              6, 4
    psubusb          m1, m4          ; p0-q0
    psubusb          m6, m3          ; q0-p0
    por              m1, m6          ; abs(q0-p0)
    paddusb          m1, m1          ; m1=2*abs(q0-p0)

    mova             m7, m2
    SWAP              7, 2
    mova             m6, m5
    SWAP              6, 5
    psubusb          m7, m5          ; p1-q1
    psubusb          m6, m2          ; q1-p1
    por              m7, m6          ; abs(q1-p1)
    pxor             m6, m6
    pand             m7, [pb_FE]
    psrlq            m7, 1           ; abs(q1-p1)/2
    paddusb          m7, m1          ; abs(q0-p0)*2+abs(q1-p1)/2
    psubusb          m7, m_flimE
    pcmpeqb          m7, m6          ; abs(q0-p0)*2+abs(q1-p1)/2 <= E
    pand             m0, m7          ; normal_limit result

    ; filter_common; at this point, m2-m5=p1-q1 and m0 is filter_mask
%ifdef m8 ; x86-64 && sse2
    mova             m8, [pb_80]
%define m_pb_80 m8
%else ; x86-32 or mmx/mmxext
%define m_pb_80 [pb_80]
%endif
    mova             m1, m4
    mova             m7, m3
    pxor             m1, m_pb_80
    pxor             m7, m_pb_80
    psubsb           m1, m7          ; (signed) q0-p0
    mova             m6, m2
    mova             m7, m5
    pxor             m6, m_pb_80
    pxor             m7, m_pb_80
    psubsb           m6, m7          ; (signed) p1-q1
    mova             m7, m_maskres
    paddsb           m6, m1
    paddsb           m6, m1
    paddsb           m6, m1
    pand             m6, m0
%ifdef m8
    mova       m_limres, m6          ; 3*(qp-p0)+(p1-q1) masked for filter_mbedge
    pand       m_limres, m7
%else
    mova             m0, m6
    pand             m0, m7
    mova       m_limres, m0
%endif
    pandn            m7, m6          ; 3*(q0-p0)+(p1-q1) masked for filter_common

    mova             m1, [pb_F8]
    mova             m6, m7
    paddsb           m7, [pb_3]
    paddsb           m6, [pb_4]
    pand             m7, m1
    pand             m6, m1

    pxor             m1, m1
    pxor             m0, m0
    pcmpgtb          m1, m7
    psubb            m0, m7
    psrlq            m7, 3           ; +f2
    psrlq            m0, 3           ; -f2
    pand             m0, m1
    pandn            m1, m7
    psubusb          m3, m0
    paddusb          m3, m1          ; p0+f2

    pxor             m1, m1
    pxor             m0, m0
    pcmpgtb          m0, m6
    psubb            m1, m6
    psrlq            m6, 3           ; +f1
    psrlq            m1, 3           ; -f1
    pand             m1, m0
    pandn            m0, m6
    psubusb          m4, m0
    paddusb          m4, m1          ; q0-f1

    ; filter_mbedge (m2-m5 = p1-q1; lim_res carries w)
%if cpuflag(ssse3)
    mova             m7, [pb_1]
%else
    mova             m7, [pw_63]
%endif
%ifdef m8
    SWAP              1, 8
%else
    mova             m1, m_limres
%endif
    pxor             m0, m0
    mova             m6, m1
    pcmpgtb          m0, m1         ; which are negative
%if cpuflag(ssse3)
    punpcklbw        m6, m7         ; interleave with "1" for rounding
    punpckhbw        m1, m7
%else
    punpcklbw        m6, m0         ; signed byte->word
    punpckhbw        m1, m0
%endif
    mova      m_limsign, m0
%if cpuflag(ssse3)
    mova             m7, [pb_27_63]
%ifndef m8
    mova       m_limres, m1
%endif
%ifdef m10
    SWAP              0, 10         ; don't lose lim_sign copy
%endif
    mova             m0, m7
    pmaddubsw        m7, m6
    SWAP              6, 7
    pmaddubsw        m0, m1
    SWAP              1, 0
%ifdef m10
    SWAP              0, 10
%else
    mova             m0, m_limsign
%endif
%else
    mova      m_maskres, m6         ; backup for later in filter
    mova       m_limres, m1
    pmullw          m6, [pw_27]
    pmullw          m1, [pw_27]
    paddw           m6, m7
    paddw           m1, m7
%endif
    psraw           m6, 7
    psraw           m1, 7
    packsswb        m6, m1          ; a0
    pxor            m1, m1
    psubb           m1, m6
    pand            m1, m0          ; -a0
    pandn           m0, m6          ; +a0
%if cpuflag(ssse3)
    mova            m6, [pb_18_63]  ; pipelining
%endif
    psubusb         m3, m1
    paddusb         m4, m1
    paddusb         m3, m0          ; p0+a0
    psubusb         m4, m0          ; q0-a0

%if cpuflag(ssse3)
    SWAP             6, 7
%ifdef m10
    SWAP             1, 10
%else
    mova            m1, m_limres
%endif
    mova            m0, m7
    pmaddubsw       m7, m6
    SWAP             6, 7
    pmaddubsw       m0, m1
    SWAP             1, 0
%ifdef m10
    SWAP             0, 10
%endif
    mova            m0, m_limsign
%else
    mova            m6, m_maskres
    mova            m1, m_limres
    pmullw          m6, [pw_18]
    pmullw          m1, [pw_18]
    paddw           m6, m7
    paddw           m1, m7
%endif
    mova            m0, m_limsign
    psraw           m6, 7
    psraw           m1, 7
    packsswb        m6, m1          ; a1
    pxor            m1, m1
    psubb           m1, m6
    pand            m1, m0          ; -a1
    pandn           m0, m6          ; +a1
%if cpuflag(ssse3)
    mova            m6, [pb_9_63]
%endif
    psubusb         m2, m1
    paddusb         m5, m1
    paddusb         m2, m0          ; p1+a1
    psubusb         m5, m0          ; q1-a1

%if cpuflag(ssse3)
    SWAP             6, 7
%ifdef m10
    SWAP             1, 10
%else
    mova            m1, m_limres
%endif
    mova            m0, m7
    pmaddubsw       m7, m6
    SWAP             6, 7
    pmaddubsw       m0, m1
    SWAP             1, 0
%else
%ifdef m8
    SWAP             6, 12
    SWAP             1, 8
%else
    mova            m6, m_maskres
    mova            m1, m_limres
%endif
    pmullw          m6, [pw_9]
    pmullw          m1, [pw_9]
    paddw           m6, m7
    paddw           m1, m7
%endif
%ifdef m9
    SWAP             7, 9
%else
    mova            m7, m_limsign
%endif
    psraw           m6, 7
    psraw           m1, 7
    packsswb        m6, m1          ; a1
    pxor            m0, m0
    psubb           m0, m6
    pand            m0, m7          ; -a1
    pandn           m7, m6          ; +a1
%ifdef m8
    SWAP             1, 13
    SWAP             6, 14
%else
    mova            m1, m_p2backup
    mova            m6, m_q2backup
%endif
    psubusb         m1, m0
    paddusb         m6, m0
    paddusb         m1, m7          ; p1+a1
    psubusb         m6, m7          ; q1-a1

    ; store
%ifidn %1, v
    movrow [dst2q+mstrideq*4], m1
    movrow [dst1q+mstrideq*2], m2
    movrow [dst1q+mstrideq  ], m3
    movrow     [dst1q], m4
    movrow     [dst2q], m5
    movrow [dst2q+ strideq  ], m6
%if mmsize == 16 && %2 == 8
    add           dst8q, mstrideq
    movhps [dst8q+mstrideq*2], m1
    movhps [dst8q+mstrideq  ], m2
    movhps     [dst8q], m3
    add          dst8q, strideq
    movhps     [dst8q], m4
    movhps [dst8q+ strideq  ], m5
    movhps [dst8q+ strideq*2], m6
%endif
%else ; h
    inc          dst1q
    inc          dst2q

    ; 4x8/16 transpose
    TRANSPOSE4x4B    1, 2, 3, 4, 0
    SBUTTERFLY      bw, 5, 6, 0

%if mmsize == 8 ; mmx/mmxext (h)
    WRITE_4x2D       1, 2, 3, 4, dst1q, dst2q, mstrideq, strideq
    add          dst1q, 4
    WRITE_2x4W      m5, m6, dst2q, dst1q, mstrideq, strideq
%else ; sse2 (h)
    lea          dst8q, [dst8q+mstrideq+1]
    WRITE_4x4D       1, 2, 3, 4, dst1q, dst2q, dst8q, mstrideq, strideq, %2
    lea          dst1q, [dst2q+mstrideq+4]
    lea          dst8q, [dst8q+mstrideq+4]
%if cpuflag(sse4)
    add          dst2q, 4
%endif
    WRITE_8W        m5, dst2q, dst1q,  mstrideq, strideq
%if cpuflag(sse4)
    lea          dst2q, [dst8q+ strideq  ]
%endif
    WRITE_8W        m6, dst2q, dst8q, mstrideq, strideq
%endif
%endif

%if mmsize == 8
%if %2 == 8 ; chroma
%ifidn %1, h
    sub          dst1q, 5
%endif
    cmp          dst1q, dst8q
    mov          dst1q, dst8q
    jnz .next8px
%else
%ifidn %1, h
    lea          dst1q, [dst1q+ strideq*8-5]
%else ; v
    add          dst1q, 8
%endif
    dec          cntrq
    jg .next8px
%endif
    REP_RET
%else ; mmsize == 16
    RET
%endif
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
MBEDGE_LOOPFILTER v, 16
MBEDGE_LOOPFILTER h, 16
MBEDGE_LOOPFILTER v,  8
MBEDGE_LOOPFILTER h,  8

INIT_MMX mmxext
MBEDGE_LOOPFILTER v, 16
MBEDGE_LOOPFILTER h, 16
MBEDGE_LOOPFILTER v,  8
MBEDGE_LOOPFILTER h,  8
%endif

INIT_XMM sse2
MBEDGE_LOOPFILTER v, 16
MBEDGE_LOOPFILTER h, 16
MBEDGE_LOOPFILTER v,  8
MBEDGE_LOOPFILTER h,  8

INIT_XMM ssse3
MBEDGE_LOOPFILTER v, 16
MBEDGE_LOOPFILTER h, 16
MBEDGE_LOOPFILTER v,  8
MBEDGE_LOOPFILTER h,  8

INIT_XMM sse4
MBEDGE_LOOPFILTER h, 16
MBEDGE_LOOPFILTER h,  8
