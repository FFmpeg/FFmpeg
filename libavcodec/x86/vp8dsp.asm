;******************************************************************************
;* VP8 MMXEXT optimizations
;* Copyright (c) 2010 Ronald S. Bultje <rsbultje@gmail.com>
;* Copyright (c) 2010 Fiona Glaser <fiona@x264.com>
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

fourtap_filter_hw_m: times 4 dw  -6, 123
                     times 4 dw  12,  -1
                     times 4 dw  -9,  93
                     times 4 dw  50,  -6
                     times 4 dw  -6,  50
                     times 4 dw  93,  -9
                     times 4 dw  -1,  12
                     times 4 dw 123,  -6

sixtap_filter_hw_m:  times 4 dw   2, -11
                     times 4 dw 108,  36
                     times 4 dw  -8,   1
                     times 4 dw   3, -16
                     times 4 dw  77,  77
                     times 4 dw -16,   3
                     times 4 dw   1,  -8
                     times 4 dw  36, 108
                     times 4 dw -11,   2

fourtap_filter_hb_m: times 8 db  -6, 123
                     times 8 db  12,  -1
                     times 8 db  -9,  93
                     times 8 db  50,  -6
                     times 8 db  -6,  50
                     times 8 db  93,  -9
                     times 8 db  -1,  12
                     times 8 db 123,  -6

sixtap_filter_hb_m:  times 8 db   2,   1
                     times 8 db -11, 108
                     times 8 db  36,  -8
                     times 8 db   3,   3
                     times 8 db -16,  77
                     times 8 db  77, -16
                     times 8 db   1,   2
                     times 8 db  -8,  36
                     times 8 db 108, -11

fourtap_filter_v_m:  times 8 dw  -6
                     times 8 dw 123
                     times 8 dw  12
                     times 8 dw  -1
                     times 8 dw  -9
                     times 8 dw  93
                     times 8 dw  50
                     times 8 dw  -6
                     times 8 dw  -6
                     times 8 dw  50
                     times 8 dw  93
                     times 8 dw  -9
                     times 8 dw  -1
                     times 8 dw  12
                     times 8 dw 123
                     times 8 dw  -6

sixtap_filter_v_m:   times 8 dw   2
                     times 8 dw -11
                     times 8 dw 108
                     times 8 dw  36
                     times 8 dw  -8
                     times 8 dw   1
                     times 8 dw   3
                     times 8 dw -16
                     times 8 dw  77
                     times 8 dw  77
                     times 8 dw -16
                     times 8 dw   3
                     times 8 dw   1
                     times 8 dw  -8
                     times 8 dw  36
                     times 8 dw 108
                     times 8 dw -11
                     times 8 dw   2

bilinear_filter_vw_m: times 8 dw 1
                      times 8 dw 2
                      times 8 dw 3
                      times 8 dw 4
                      times 8 dw 5
                      times 8 dw 6
                      times 8 dw 7

bilinear_filter_vb_m: times 8 db 7, 1
                      times 8 db 6, 2
                      times 8 db 5, 3
                      times 8 db 4, 4
                      times 8 db 3, 5
                      times 8 db 2, 6
                      times 8 db 1, 7

%if PIC
%define fourtap_filter_hw  picregq
%define sixtap_filter_hw   picregq
%define fourtap_filter_hb  picregq
%define sixtap_filter_hb   picregq
%define fourtap_filter_v   picregq
%define sixtap_filter_v    picregq
%define bilinear_filter_vw picregq
%define bilinear_filter_vb picregq
%define npicregs 1
%else
%define fourtap_filter_hw  fourtap_filter_hw_m
%define sixtap_filter_hw   sixtap_filter_hw_m
%define fourtap_filter_hb  fourtap_filter_hb_m
%define sixtap_filter_hb   sixtap_filter_hb_m
%define fourtap_filter_v   fourtap_filter_v_m
%define sixtap_filter_v    sixtap_filter_v_m
%define bilinear_filter_vw bilinear_filter_vw_m
%define bilinear_filter_vb bilinear_filter_vb_m
%define npicregs 0
%endif

filter_h2_shuf:  db 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5,  6, 6,  7,  7,  8
filter_h4_shuf:  db 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,  8, 8,  9,  9, 10

filter_h6_shuf1: db 0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 5, 10, 6, 11,  7, 12
filter_h6_shuf2: db 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,  7, 7,  8,  8,  9
filter_h6_shuf3: db 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,  9, 9, 10, 10, 11

pw_20091: times 4 dw 20091
pw_17734: times 4 dw 17734

cextern pw_3
cextern pw_4
cextern pw_64
cextern pw_256

SECTION .text

;-------------------------------------------------------------------------------
; subpel MC functions:
;
; void ff_put_vp8_epel<size>_h<htap>v<vtap>_<opt>(uint8_t *dst, ptrdiff_t deststride,
;                                                 const uint8_t *src, ptrdiff_t srcstride,
;                                                 int height,   int mx, int my);
;-------------------------------------------------------------------------------

%macro FILTER_SSSE3 1
cglobal put_vp8_epel%1_h6, 6, 6 + npicregs, 8, dst, dststride, src, srcstride, height, mx, picreg
    lea      mxd, [mxq*3]
    mova      m3, [filter_h6_shuf2]
    mova      m4, [filter_h6_shuf3]
%if PIC
    lea  picregq, [sixtap_filter_hb_m]
%endif
    mova      m5, [sixtap_filter_hb+mxq*8-48] ; set up 6tap filter in bytes
    mova      m6, [sixtap_filter_hb+mxq*8-32]
    mova      m7, [sixtap_filter_hb+mxq*8-16]

.nextrow:
    movu      m0, [srcq-2]
    mova      m1, m0
    mova      m2, m0
%if mmsize == 8
; For epel4, we need 9 bytes, but only 8 get loaded; to compensate, do the
; shuffle with a memory operand
    punpcklbw m0, [srcq+3]
%else
    pshufb    m0, [filter_h6_shuf1]
%endif
    pshufb    m1, m3
    pshufb    m2, m4
    pmaddubsw m0, m5
    pmaddubsw m1, m6
    pmaddubsw m2, m7
    paddsw    m0, m1
    paddsw    m0, m2
    pmulhrsw  m0, [pw_256]
    packuswb  m0, m0
    movh  [dstq], m0        ; store

    ; go to next line
    add     dstq, dststrideq
    add     srcq, srcstrideq
    dec  heightd            ; next row
    jg .nextrow
    RET

cglobal put_vp8_epel%1_h4, 6, 6 + npicregs, 7, dst, dststride, src, srcstride, height, mx, picreg
    shl      mxd, 4
    mova      m2, [pw_256]
    mova      m3, [filter_h2_shuf]
    mova      m4, [filter_h4_shuf]
%if PIC
    lea  picregq, [fourtap_filter_hb_m]
%endif
    mova      m5, [fourtap_filter_hb+mxq-16] ; set up 4tap filter in bytes
    mova      m6, [fourtap_filter_hb+mxq]

.nextrow:
    movu      m0, [srcq-1]
    mova      m1, m0
    pshufb    m0, m3
    pshufb    m1, m4
    pmaddubsw m0, m5
    pmaddubsw m1, m6
    paddsw    m0, m1
    pmulhrsw  m0, m2
    packuswb  m0, m0
    movh  [dstq], m0        ; store

    ; go to next line
    add     dstq, dststrideq
    add     srcq, srcstrideq
    dec  heightd            ; next row
    jg .nextrow
    RET

cglobal put_vp8_epel%1_v4, 7, 7, 8, dst, dststride, src, srcstride, height, picreg, my
    shl      myd, 4
%if PIC
    lea  picregq, [fourtap_filter_hb_m]
%endif
    mova      m5, [fourtap_filter_hb+myq-16]
    mova      m6, [fourtap_filter_hb+myq]
    mova      m7, [pw_256]

    ; read 3 lines
    sub     srcq, srcstrideq
    movh      m0, [srcq]
    movh      m1, [srcq+  srcstrideq]
    movh      m2, [srcq+2*srcstrideq]
    add     srcq, srcstrideq

.nextrow:
    movh      m3, [srcq+2*srcstrideq]      ; read new row
    mova      m4, m0
    mova      m0, m1
    punpcklbw m4, m1
    mova      m1, m2
    punpcklbw m2, m3
    pmaddubsw m4, m5
    pmaddubsw m2, m6
    paddsw    m4, m2
    mova      m2, m3
    pmulhrsw  m4, m7
    packuswb  m4, m4
    movh  [dstq], m4

    ; go to next line
    add      dstq, dststrideq
    add      srcq, srcstrideq
    dec   heightd                          ; next row
    jg .nextrow
    RET

cglobal put_vp8_epel%1_v6, 7, 7, 8, dst, dststride, src, srcstride, height, picreg, my
    lea      myd, [myq*3]
%if PIC
    lea  picregq, [sixtap_filter_hb_m]
%endif
    lea      myq, [sixtap_filter_hb+myq*8]

    ; read 5 lines
    sub     srcq, srcstrideq
    sub     srcq, srcstrideq
    movh      m0, [srcq]
    movh      m1, [srcq+srcstrideq]
    movh      m2, [srcq+srcstrideq*2]
    lea     srcq, [srcq+srcstrideq*2]
    add     srcq, srcstrideq
    movh      m3, [srcq]
    movh      m4, [srcq+srcstrideq]

.nextrow:
    movh      m5, [srcq+2*srcstrideq]      ; read new row
    mova      m6, m0
    punpcklbw m6, m5
    mova      m0, m1
    punpcklbw m1, m2
    mova      m7, m3
    punpcklbw m7, m4
    pmaddubsw m6, [myq-48]
    pmaddubsw m1, [myq-32]
    pmaddubsw m7, [myq-16]
    paddsw    m6, m1
    paddsw    m6, m7
    mova      m1, m2
    mova      m2, m3
    pmulhrsw  m6, [pw_256]
    mova      m3, m4
    packuswb  m6, m6
    mova      m4, m5
    movh  [dstq], m6

    ; go to next line
    add      dstq, dststrideq
    add      srcq, srcstrideq
    dec   heightd                          ; next row
    jg .nextrow
    RET
%endmacro

INIT_MMX ssse3
FILTER_SSSE3 4
INIT_XMM ssse3
FILTER_SSSE3 8

; 4x4 block, H-only 4-tap filter
INIT_MMX mmxext
cglobal put_vp8_epel4_h4, 6, 6 + npicregs, 0, dst, dststride, src, srcstride, height, mx, picreg
    shl       mxd, 4
%if PIC
    lea   picregq, [fourtap_filter_hw_m]
%endif
    movq      mm4, [fourtap_filter_hw+mxq-16] ; set up 4tap filter in words
    movq      mm5, [fourtap_filter_hw+mxq]
    movq      mm7, [pw_64]
    pxor      mm6, mm6

.nextrow:
    movq      mm1, [srcq-1]                ; (ABCDEFGH) load 8 horizontal pixels

    ; first set of 2 pixels
    movq      mm2, mm1                     ; byte ABCD..
    punpcklbw mm1, mm6                     ; byte->word ABCD
    pshufw    mm0, mm2, 9                  ; byte CDEF..
    punpcklbw mm0, mm6                     ; byte->word CDEF
    pshufw    mm3, mm1, 0x94               ; word ABBC
    pshufw    mm1, mm0, 0x94               ; word CDDE
    pmaddwd   mm3, mm4                     ; multiply 2px with F0/F1
    movq      mm0, mm1                     ; backup for second set of pixels
    pmaddwd   mm1, mm5                     ; multiply 2px with F2/F3
    paddd     mm3, mm1                     ; finish 1st 2px

    ; second set of 2 pixels, use backup of above
    punpckhbw mm2, mm6                     ; byte->word EFGH
    pmaddwd   mm0, mm4                     ; multiply backed up 2px with F0/F1
    pshufw    mm1, mm2, 0x94               ; word EFFG
    pmaddwd   mm1, mm5                     ; multiply 2px with F2/F3
    paddd     mm0, mm1                     ; finish 2nd 2px

    ; merge two sets of 2 pixels into one set of 4, round/clip/store
    packssdw  mm3, mm0                     ; merge dword->word (4px)
    paddsw    mm3, mm7                     ; rounding
    psraw     mm3, 7
    packuswb  mm3, mm6                     ; clip and word->bytes
    movd   [dstq], mm3                     ; store

    ; go to next line
    add      dstq, dststrideq
    add      srcq, srcstrideq
    dec   heightd                          ; next row
    jg .nextrow
    RET

; 4x4 block, H-only 6-tap filter
INIT_MMX mmxext
cglobal put_vp8_epel4_h6, 6, 6 + npicregs, 0, dst, dststride, src, srcstride, height, mx, picreg
    lea       mxd, [mxq*3]
%if PIC
    lea   picregq, [sixtap_filter_hw_m]
%endif
    movq      mm4, [sixtap_filter_hw+mxq*8-48] ; set up 4tap filter in words
    movq      mm5, [sixtap_filter_hw+mxq*8-32]
    movq      mm6, [sixtap_filter_hw+mxq*8-16]
    movq      mm7, [pw_64]
    pxor      mm3, mm3

.nextrow:
    movq      mm1, [srcq-2]                ; (ABCDEFGH) load 8 horizontal pixels

    ; first set of 2 pixels
    movq      mm2, mm1                     ; byte ABCD..
    punpcklbw mm1, mm3                     ; byte->word ABCD
    pshufw    mm0, mm2, 0x9                ; byte CDEF..
    punpckhbw mm2, mm3                     ; byte->word EFGH
    punpcklbw mm0, mm3                     ; byte->word CDEF
    pshufw    mm1, mm1, 0x94               ; word ABBC
    pshufw    mm2, mm2, 0x94               ; word EFFG
    pmaddwd   mm1, mm4                     ; multiply 2px with F0/F1
    pshufw    mm3, mm0, 0x94               ; word CDDE
    movq      mm0, mm3                     ; backup for second set of pixels
    pmaddwd   mm3, mm5                     ; multiply 2px with F2/F3
    paddd     mm1, mm3                     ; add to 1st 2px cache
    movq      mm3, mm2                     ; backup for second set of pixels
    pmaddwd   mm2, mm6                     ; multiply 2px with F4/F5
    paddd     mm1, mm2                     ; finish 1st 2px

    ; second set of 2 pixels, use backup of above
    movd      mm2, [srcq+3]                ; byte FGHI (prevent overreads)
    pmaddwd   mm0, mm4                     ; multiply 1st backed up 2px with F0/F1
    pmaddwd   mm3, mm5                     ; multiply 2nd backed up 2px with F2/F3
    paddd     mm0, mm3                     ; add to 2nd 2px cache
    pxor      mm3, mm3
    punpcklbw mm2, mm3                     ; byte->word FGHI
    pshufw    mm2, mm2, 0xE9               ; word GHHI
    pmaddwd   mm2, mm6                     ; multiply 2px with F4/F5
    paddd     mm0, mm2                     ; finish 2nd 2px

    ; merge two sets of 2 pixels into one set of 4, round/clip/store
    packssdw  mm1, mm0                     ; merge dword->word (4px)
    paddsw    mm1, mm7                     ; rounding
    psraw     mm1, 7
    packuswb  mm1, mm3                     ; clip and word->bytes
    movd   [dstq], mm1                     ; store

    ; go to next line
    add      dstq, dststrideq
    add      srcq, srcstrideq
    dec   heightd                          ; next row
    jg .nextrow
    RET

INIT_XMM sse2
cglobal put_vp8_epel8_h4, 6, 6 + npicregs, 10, dst, dststride, src, srcstride, height, mx, picreg
    shl      mxd, 5
%if PIC
    lea  picregq, [fourtap_filter_v_m]
%endif
    lea      mxq, [fourtap_filter_v+mxq-32]
    pxor      m7, m7
    mova      m4, [pw_64]
    mova      m5, [mxq+ 0]
    mova      m6, [mxq+16]
%ifdef m8
    mova      m8, [mxq+32]
    mova      m9, [mxq+48]
%endif
.nextrow:
    movq      m0, [srcq-1]
    movq      m1, [srcq-0]
    movq      m2, [srcq+1]
    movq      m3, [srcq+2]
    punpcklbw m0, m7
    punpcklbw m1, m7
    punpcklbw m2, m7
    punpcklbw m3, m7
    pmullw    m0, m5
    pmullw    m1, m6
%ifdef m8
    pmullw    m2, m8
    pmullw    m3, m9
%else
    pmullw    m2, [mxq+32]
    pmullw    m3, [mxq+48]
%endif
    paddsw    m0, m1
    paddsw    m2, m3
    paddsw    m0, m2
    paddsw    m0, m4
    psraw     m0, 7
    packuswb  m0, m7
    movh  [dstq], m0        ; store

    ; go to next line
    add     dstq, dststrideq
    add     srcq, srcstrideq
    dec  heightd            ; next row
    jg .nextrow
    RET

INIT_XMM sse2
cglobal put_vp8_epel8_h6, 6, 6 + npicregs, 14, dst, dststride, src, srcstride, height, mx, picreg
    lea      mxd, [mxq*3]
    shl      mxd, 4
%if PIC
    lea  picregq, [sixtap_filter_v_m]
%endif
    lea      mxq, [sixtap_filter_v+mxq-96]
    pxor      m7, m7
    mova      m6, [pw_64]
%ifdef m8
    mova      m8, [mxq+ 0]
    mova      m9, [mxq+16]
    mova     m10, [mxq+32]
    mova     m11, [mxq+48]
    mova     m12, [mxq+64]
    mova     m13, [mxq+80]
%endif
.nextrow:
    movq      m0, [srcq-2]
    movq      m1, [srcq-1]
    movq      m2, [srcq-0]
    movq      m3, [srcq+1]
    movq      m4, [srcq+2]
    movq      m5, [srcq+3]
    punpcklbw m0, m7
    punpcklbw m1, m7
    punpcklbw m2, m7
    punpcklbw m3, m7
    punpcklbw m4, m7
    punpcklbw m5, m7
%ifdef m8
    pmullw    m0, m8
    pmullw    m1, m9
    pmullw    m2, m10
    pmullw    m3, m11
    pmullw    m4, m12
    pmullw    m5, m13
%else
    pmullw    m0, [mxq+ 0]
    pmullw    m1, [mxq+16]
    pmullw    m2, [mxq+32]
    pmullw    m3, [mxq+48]
    pmullw    m4, [mxq+64]
    pmullw    m5, [mxq+80]
%endif
    paddsw    m1, m4
    paddsw    m0, m5
    paddsw    m1, m2
    paddsw    m0, m3
    paddsw    m0, m1
    paddsw    m0, m6
    psraw     m0, 7
    packuswb  m0, m7
    movh  [dstq], m0        ; store

    ; go to next line
    add     dstq, dststrideq
    add     srcq, srcstrideq
    dec  heightd            ; next row
    jg .nextrow
    RET

%macro FILTER_V 1
; 4x4 block, V-only 4-tap filter
cglobal put_vp8_epel%1_v4, 7, 7, 8, dst, dststride, src, srcstride, height, picreg, my
    shl      myd, 5
%if PIC
    lea  picregq, [fourtap_filter_v_m]
%endif
    lea      myq, [fourtap_filter_v+myq-32]
    mova      m6, [pw_64]
    pxor      m7, m7
    mova      m5, [myq+48]

    ; read 3 lines
    sub     srcq, srcstrideq
    movh      m0, [srcq]
    movh      m1, [srcq+  srcstrideq]
    movh      m2, [srcq+2*srcstrideq]
    add     srcq, srcstrideq
    punpcklbw m0, m7
    punpcklbw m1, m7
    punpcklbw m2, m7

.nextrow:
    ; first calculate negative taps (to prevent losing positive overflows)
    movh      m4, [srcq+2*srcstrideq]      ; read new row
    punpcklbw m4, m7
    mova      m3, m4
    pmullw    m0, [myq+0]
    pmullw    m4, m5
    paddsw    m4, m0

    ; then calculate positive taps
    mova      m0, m1
    pmullw    m1, [myq+16]
    paddsw    m4, m1
    mova      m1, m2
    pmullw    m2, [myq+32]
    paddsw    m4, m2
    mova      m2, m3

    ; round/clip/store
    paddsw    m4, m6
    psraw     m4, 7
    packuswb  m4, m7
    movh  [dstq], m4

    ; go to next line
    add     dstq, dststrideq
    add     srcq, srcstrideq
    dec  heightd                           ; next row
    jg .nextrow
    RET


; 4x4 block, V-only 6-tap filter
cglobal put_vp8_epel%1_v6, 7, 7, 8, dst, dststride, src, srcstride, height, picreg, my
    shl      myd, 4
    lea      myq, [myq*3]
%if PIC
    lea  picregq, [sixtap_filter_v_m]
%endif
    lea      myq, [sixtap_filter_v+myq-96]
    pxor      m7, m7

    ; read 5 lines
    sub     srcq, srcstrideq
    sub     srcq, srcstrideq
    movh      m0, [srcq]
    movh      m1, [srcq+srcstrideq]
    movh      m2, [srcq+srcstrideq*2]
    lea     srcq, [srcq+srcstrideq*2]
    add     srcq, srcstrideq
    movh      m3, [srcq]
    movh      m4, [srcq+srcstrideq]
    punpcklbw m0, m7
    punpcklbw m1, m7
    punpcklbw m2, m7
    punpcklbw m3, m7
    punpcklbw m4, m7

.nextrow:
    ; first calculate negative taps (to prevent losing positive overflows)
    mova      m5, m1
    pmullw    m5, [myq+16]
    mova      m6, m4
    pmullw    m6, [myq+64]
    paddsw    m6, m5

    ; then calculate positive taps
    movh      m5, [srcq+2*srcstrideq]      ; read new row
    punpcklbw m5, m7
    pmullw    m0, [myq+0]
    paddsw    m6, m0
    mova      m0, m1
    mova      m1, m2
    pmullw    m2, [myq+32]
    paddsw    m6, m2
    mova      m2, m3
    pmullw    m3, [myq+48]
    paddsw    m6, m3
    mova      m3, m4
    mova      m4, m5
    pmullw    m5, [myq+80]
    paddsw    m6, m5

    ; round/clip/store
    paddsw    m6, [pw_64]
    psraw     m6, 7
    packuswb  m6, m7
    movh  [dstq], m6

    ; go to next line
    add     dstq, dststrideq
    add     srcq, srcstrideq
    dec  heightd                           ; next row
    jg .nextrow
    RET
%endmacro

INIT_MMX mmxext
FILTER_V 4
INIT_XMM sse2
FILTER_V 8

%macro FILTER_BILINEAR 1
%if cpuflag(ssse3)
cglobal put_vp8_bilinear%1_v, 7, 7, 5, dst, dststride, src, srcstride, height, picreg, my
    shl      myd, 4
%if PIC
    lea  picregq, [bilinear_filter_vb_m]
%endif
    pxor      m4, m4
    mova      m3, [bilinear_filter_vb+myq-16]
.nextrow:
    movh      m0, [srcq+srcstrideq*0]
    movh      m1, [srcq+srcstrideq*1]
    movh      m2, [srcq+srcstrideq*2]
    punpcklbw m0, m1
    punpcklbw m1, m2
    pmaddubsw m0, m3
    pmaddubsw m1, m3
    psraw     m0, 2
    psraw     m1, 2
    pavgw     m0, m4
    pavgw     m1, m4
%if mmsize==8
    packuswb  m0, m0
    packuswb  m1, m1
    movh   [dstq+dststrideq*0], m0
    movh   [dstq+dststrideq*1], m1
%else
    packuswb  m0, m1
    movh   [dstq+dststrideq*0], m0
    movhps [dstq+dststrideq*1], m0
%endif
%else ; cpuflag(ssse3)
cglobal put_vp8_bilinear%1_v, 7, 7, 7, dst, dststride, src, srcstride, height, picreg, my
    shl      myd, 4
%if PIC
    lea  picregq, [bilinear_filter_vw_m]
%endif
    pxor      m6, m6
    mova      m5, [bilinear_filter_vw+myq-1*16]
    neg      myq
    mova      m4, [bilinear_filter_vw+myq+7*16]
.nextrow:
    movh      m0, [srcq+srcstrideq*0]
    movh      m1, [srcq+srcstrideq*1]
    movh      m3, [srcq+srcstrideq*2]
    punpcklbw m0, m6
    punpcklbw m1, m6
    punpcklbw m3, m6
    mova      m2, m1
    pmullw    m0, m4
    pmullw    m1, m5
    pmullw    m2, m4
    pmullw    m3, m5
    paddsw    m0, m1
    paddsw    m2, m3
    psraw     m0, 2
    psraw     m2, 2
    pavgw     m0, m6
    pavgw     m2, m6
%if mmsize == 8
    packuswb  m0, m0
    packuswb  m2, m2
    movh   [dstq+dststrideq*0], m0
    movh   [dstq+dststrideq*1], m2
%else
    packuswb  m0, m2
    movh   [dstq+dststrideq*0], m0
    movhps [dstq+dststrideq*1], m0
%endif
%endif ; cpuflag(ssse3)

    lea     dstq, [dstq+dststrideq*2]
    lea     srcq, [srcq+srcstrideq*2]
    sub  heightd, 2
    jg .nextrow
    RET

%if cpuflag(ssse3)
cglobal put_vp8_bilinear%1_h, 6, 6 + npicregs, 5, dst, dststride, src, srcstride, height, mx, picreg
    shl      mxd, 4
%if PIC
    lea  picregq, [bilinear_filter_vb_m]
%endif
    pxor      m4, m4
    mova      m2, [filter_h2_shuf]
    mova      m3, [bilinear_filter_vb+mxq-16]
.nextrow:
    movu      m0, [srcq+srcstrideq*0]
    movu      m1, [srcq+srcstrideq*1]
    pshufb    m0, m2
    pshufb    m1, m2
    pmaddubsw m0, m3
    pmaddubsw m1, m3
    psraw     m0, 2
    psraw     m1, 2
    pavgw     m0, m4
    pavgw     m1, m4
%if mmsize==8
    packuswb  m0, m0
    packuswb  m1, m1
    movh   [dstq+dststrideq*0], m0
    movh   [dstq+dststrideq*1], m1
%else
    packuswb  m0, m1
    movh   [dstq+dststrideq*0], m0
    movhps [dstq+dststrideq*1], m0
%endif
%else ; cpuflag(ssse3)
cglobal put_vp8_bilinear%1_h, 6, 6 + npicregs, 7, dst, dststride, src, srcstride, height, mx, picreg
    shl      mxd, 4
%if PIC
    lea  picregq, [bilinear_filter_vw_m]
%endif
    pxor      m6, m6
    mova      m5, [bilinear_filter_vw+mxq-1*16]
    neg      mxq
    mova      m4, [bilinear_filter_vw+mxq+7*16]
.nextrow:
    movh      m0, [srcq+srcstrideq*0+0]
    movh      m1, [srcq+srcstrideq*0+1]
    movh      m2, [srcq+srcstrideq*1+0]
    movh      m3, [srcq+srcstrideq*1+1]
    punpcklbw m0, m6
    punpcklbw m1, m6
    punpcklbw m2, m6
    punpcklbw m3, m6
    pmullw    m0, m4
    pmullw    m1, m5
    pmullw    m2, m4
    pmullw    m3, m5
    paddsw    m0, m1
    paddsw    m2, m3
    psraw     m0, 2
    psraw     m2, 2
    pavgw     m0, m6
    pavgw     m2, m6
%if mmsize == 8
    packuswb  m0, m0
    packuswb  m2, m2
    movh   [dstq+dststrideq*0], m0
    movh   [dstq+dststrideq*1], m2
%else
    packuswb  m0, m2
    movh   [dstq+dststrideq*0], m0
    movhps [dstq+dststrideq*1], m0
%endif
%endif ; cpuflag(ssse3)

    lea     dstq, [dstq+dststrideq*2]
    lea     srcq, [srcq+srcstrideq*2]
    sub  heightd, 2
    jg .nextrow
    RET
%endmacro

INIT_MMX mmxext
FILTER_BILINEAR 4
INIT_XMM sse2
FILTER_BILINEAR 8
INIT_MMX ssse3
FILTER_BILINEAR 4
INIT_XMM ssse3
FILTER_BILINEAR 8

INIT_MMX mmx
cglobal put_vp8_pixels8, 5, 5, 0, dst, dststride, src, srcstride, height
.nextrow:
    movq    mm0, [srcq+srcstrideq*0]
    movq    mm1, [srcq+srcstrideq*1]
    lea    srcq, [srcq+srcstrideq*2]
    movq [dstq+dststrideq*0], mm0
    movq [dstq+dststrideq*1], mm1
    lea    dstq, [dstq+dststrideq*2]
    sub heightd, 2
    jg .nextrow
    RET

INIT_XMM sse
cglobal put_vp8_pixels16, 5, 5, 2, dst, dststride, src, srcstride, height
.nextrow:
    movups xmm0, [srcq+srcstrideq*0]
    movups xmm1, [srcq+srcstrideq*1]
    lea    srcq, [srcq+srcstrideq*2]
    movaps [dstq+dststrideq*0], xmm0
    movaps [dstq+dststrideq*1], xmm1
    lea    dstq, [dstq+dststrideq*2]
    sub heightd, 2
    jg .nextrow
    RET

;-----------------------------------------------------------------------------
; void ff_vp8_idct_dc_add_<opt>(uint8_t *dst, int16_t block[16], ptrdiff_t stride);
;-----------------------------------------------------------------------------

%macro ADD_DC 4
    %4        m2, [dst1q+%3]
    %4        m3, [dst1q+strideq+%3]
    %4        m4, [dst2q+%3]
    %4        m5, [dst2q+strideq+%3]
    paddusb   m2, %1
    paddusb   m3, %1
    paddusb   m4, %1
    paddusb   m5, %1
    psubusb   m2, %2
    psubusb   m3, %2
    psubusb   m4, %2
    psubusb   m5, %2
    %4 [dst1q+%3], m2
    %4 [dst1q+strideq+%3], m3
    %4 [dst2q+%3], m4
    %4 [dst2q+strideq+%3], m5
%endmacro

%macro VP8_IDCT_DC_ADD 0
cglobal vp8_idct_dc_add, 3, 3, 6, dst, block, stride
    ; load data
    movd       m0, [blockq]
    pxor       m1, m1

    ; calculate DC
    paddw      m0, [pw_4]
    movd [blockq], m1
    DEFINE_ARGS dst1, dst2, stride
    lea     dst2q, [dst1q+strideq*2]
    movd       m2, [dst1q]
    movd       m3, [dst1q+strideq]
    movd       m4, [dst2q]
    movd       m5, [dst2q+strideq]
    psraw      m0, 3
    pshuflw    m0, m0, 0
    punpcklqdq m0, m0
    punpckldq  m2, m3
    punpckldq  m4, m5
    punpcklbw  m2, m1
    punpcklbw  m4, m1
    paddw      m2, m0
    paddw      m4, m0
    packuswb   m2, m4
    movd   [dst1q], m2
%if cpuflag(sse4)
    pextrd [dst1q+strideq], m2, 1
    pextrd [dst2q], m2, 2
    pextrd [dst2q+strideq], m2, 3
%else
    psrldq     m2, 4
    movd [dst1q+strideq], m2
    psrldq     m2, 4
    movd [dst2q], m2
    psrldq     m2, 4
    movd [dst2q+strideq], m2
%endif
    RET
%endmacro

INIT_XMM sse2
VP8_IDCT_DC_ADD
INIT_XMM sse4
VP8_IDCT_DC_ADD

;-----------------------------------------------------------------------------
; void ff_vp8_idct_dc_add4y_<opt>(uint8_t *dst, int16_t block[4][16], ptrdiff_t stride);
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal vp8_idct_dc_add4y, 3, 3, 6, dst, block, stride
    ; load data
    movd      m0, [blockq+32*0] ; A
    movd      m1, [blockq+32*2] ; C
    punpcklwd m0, [blockq+32*1] ; A B
    punpcklwd m1, [blockq+32*3] ; C D
    punpckldq m0, m1        ; A B C D
    pxor      m1, m1

    ; calculate DC
    paddw     m0, [pw_4]
    movd [blockq+32*0], m1
    movd [blockq+32*1], m1
    movd [blockq+32*2], m1
    movd [blockq+32*3], m1
    psraw     m0, 3
    psubw     m1, m0
    packuswb  m0, m0
    packuswb  m1, m1
    punpcklbw m0, m0
    punpcklbw m1, m1
    punpcklbw m0, m0
    punpcklbw m1, m1

    ; add DC
    DEFINE_ARGS dst1, dst2, stride
    lea    dst2q, [dst1q+strideq*2]
    ADD_DC    m0, m1, 0, mova
    RET

;-----------------------------------------------------------------------------
; void ff_vp8_idct_dc_add4uv_<opt>(uint8_t *dst, int16_t block[4][16], ptrdiff_t stride);
;-----------------------------------------------------------------------------

INIT_MMX mmx
cglobal vp8_idct_dc_add4uv, 3, 3, 0, dst, block, stride
    ; load data
    movd      m0, [blockq+32*0] ; A
    movd      m1, [blockq+32*2] ; C
    punpcklwd m0, [blockq+32*1] ; A B
    punpcklwd m1, [blockq+32*3] ; C D
    punpckldq m0, m1        ; A B C D
    pxor      m6, m6

    ; calculate DC
    paddw     m0, [pw_4]
    movd [blockq+32*0], m6
    movd [blockq+32*1], m6
    movd [blockq+32*2], m6
    movd [blockq+32*3], m6
    psraw     m0, 3
    psubw     m6, m0
    packuswb  m0, m0
    packuswb  m6, m6
    punpcklbw m0, m0 ; AABBCCDD
    punpcklbw m6, m6 ; AABBCCDD
    movq      m1, m0
    movq      m7, m6
    punpcklbw m0, m0 ; AAAABBBB
    punpckhbw m1, m1 ; CCCCDDDD
    punpcklbw m6, m6 ; AAAABBBB
    punpckhbw m7, m7 ; CCCCDDDD

    ; add DC
    DEFINE_ARGS dst1, dst2, stride
    lea    dst2q, [dst1q+strideq*2]
    ADD_DC    m0, m6, 0, mova
    lea    dst1q, [dst1q+strideq*4]
    lea    dst2q, [dst2q+strideq*4]
    ADD_DC    m1, m7, 0, mova
    RET

;-----------------------------------------------------------------------------
; void ff_vp8_idct_add_<opt>(uint8_t *dst, int16_t block[16], ptrdiff_t stride);
;-----------------------------------------------------------------------------

; calculate %1=mul_35468(%1)-mul_20091(%2); %2=mul_20091(%1)+mul_35468(%2)
;           this macro assumes that m6/m7 have words for 20091/17734 loaded
%macro VP8_MULTIPLY_SUMSUB 4
    mova      %3, %1
    mova      %4, %2
    pmulhw    %3, m6 ;20091(1)
    pmulhw    %4, m6 ;20091(2)
    paddw     %3, %1
    paddw     %4, %2
    paddw     %1, %1
    paddw     %2, %2
    pmulhw    %1, m7 ;35468(1)
    pmulhw    %2, m7 ;35468(2)
    psubw     %1, %4
    paddw     %2, %3
%endmacro

; calculate x0=%1+%3; x1=%1-%3
;           x2=mul_35468(%2)-mul_20091(%4); x3=mul_20091(%2)+mul_35468(%4)
;           %1=x0+x3 (tmp0); %2=x1+x2 (tmp1); %3=x1-x2 (tmp2); %4=x0-x3 (tmp3)
;           %5/%6 are temporary registers
;           we assume m6/m7 have constant words 20091/17734 loaded in them
%macro VP8_IDCT_TRANSFORM4x4_1D 6
    SUMSUB_BA         w, %3,  %1,  %5     ;t0, t1
    VP8_MULTIPLY_SUMSUB m%2, m%4, m%5,m%6 ;t2, t3
    SUMSUB_BA         w, %4,  %3,  %5     ;tmp0, tmp3
    SUMSUB_BA         w, %2,  %1,  %5     ;tmp1, tmp2
    SWAP                 %4,  %1
    SWAP                 %4,  %3
%endmacro

INIT_MMX sse
cglobal vp8_idct_add, 3, 3, 0, dst, block, stride
    ; load block data
    movq         m0, [blockq+ 0]
    movq         m1, [blockq+ 8]
    movq         m2, [blockq+16]
    movq         m3, [blockq+24]
    movq         m6, [pw_20091]
    movq         m7, [pw_17734]
    xorps      xmm0, xmm0
    movaps [blockq+ 0], xmm0
    movaps [blockq+16], xmm0

    ; actual IDCT
    VP8_IDCT_TRANSFORM4x4_1D 0, 1, 2, 3, 4, 5
    TRANSPOSE4x4W            0, 1, 2, 3, 4
    paddw        m0, [pw_4]
    VP8_IDCT_TRANSFORM4x4_1D 0, 1, 2, 3, 4, 5
    TRANSPOSE4x4W            0, 1, 2, 3, 4

    ; store
    pxor         m4, m4
    DEFINE_ARGS dst1, dst2, stride
    lea       dst2q, [dst1q+2*strideq]
    STORE_DIFFx2 m0, m1, m6, m7, m4, 3, dst1q, strideq
    STORE_DIFFx2 m2, m3, m6, m7, m4, 3, dst2q, strideq

    RET

;-----------------------------------------------------------------------------
; void ff_vp8_luma_dc_wht(int16_t block[4][4][16], int16_t dc[16])
;-----------------------------------------------------------------------------

%macro SCATTER_WHT 3
    movd dc1d, m%1
    movd dc2d, m%2
    mov [blockq+2*16*(0+%3)], dc1w
    mov [blockq+2*16*(1+%3)], dc2w
    shr  dc1d, 16
    shr  dc2d, 16
    psrlq m%1, 32
    psrlq m%2, 32
    mov [blockq+2*16*(4+%3)], dc1w
    mov [blockq+2*16*(5+%3)], dc2w
    movd dc1d, m%1
    movd dc2d, m%2
    mov [blockq+2*16*(8+%3)], dc1w
    mov [blockq+2*16*(9+%3)], dc2w
    shr  dc1d, 16
    shr  dc2d, 16
    mov [blockq+2*16*(12+%3)], dc1w
    mov [blockq+2*16*(13+%3)], dc2w
%endmacro

%macro HADAMARD4_1D 4
    SUMSUB_BADC w, %2, %1, %4, %3
    SUMSUB_BADC w, %4, %2, %3, %1
    SWAP %1, %4, %3
%endmacro

INIT_MMX sse
cglobal vp8_luma_dc_wht, 2, 3, 0, block, dc1, dc2
    movq          m0, [dc1q]
    movq          m1, [dc1q+8]
    movq          m2, [dc1q+16]
    movq          m3, [dc1q+24]
    xorps      xmm0, xmm0
    movaps [dc1q+ 0], xmm0
    movaps [dc1q+16], xmm0
    HADAMARD4_1D  0, 1, 2, 3
    TRANSPOSE4x4W 0, 1, 2, 3, 4
    paddw         m0, [pw_3]
    HADAMARD4_1D  0, 1, 2, 3
    psraw         m0, 3
    psraw         m1, 3
    psraw         m2, 3
    psraw         m3, 3
    SCATTER_WHT   0, 1, 0
    SCATTER_WHT   2, 3, 2
    RET
