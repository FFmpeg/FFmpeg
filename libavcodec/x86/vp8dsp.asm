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
;* 51, Inc., Foundation Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "x86inc.asm"
%include "x86util.asm"

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

fourtap_filter_hb_m: times 8 db  -6,  -1
                     times 8 db 123,  12
                     times 8 db  -9,  -6
                     times 8 db  93,  50
                     times 8 db  -6,  -9
                     times 8 db  50,  93
                     times 8 db  -1,  -6
                     times 8 db  12, 123

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

%ifdef PIC
%define fourtap_filter_hw    r11
%define sixtap_filter_hw     r11
%define fourtap_filter_hb    r11
%define sixtap_filter_hb     r11
%define fourtap_filter_v     r11
%define sixtap_filter_v      r11
%define bilinear_filter_vw   r11
%define bilinear_filter_vb   r11
%else
%define fourtap_filter_hw fourtap_filter_hw_m
%define sixtap_filter_hw  sixtap_filter_hw_m
%define fourtap_filter_hb fourtap_filter_hb_m
%define sixtap_filter_hb  sixtap_filter_hb_m
%define fourtap_filter_v  fourtap_filter_v_m
%define sixtap_filter_v   sixtap_filter_v_m
%define bilinear_filter_vw bilinear_filter_vw_m
%define bilinear_filter_vb bilinear_filter_vb_m
%endif

filter_h2_shuf:  db 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5,  6, 6,  7,  7,  8
filter_h4_shuf:  db 0, 3, 1, 4, 2, 5, 3, 6, 4, 7, 5,  8, 6,  9,  7, 10

filter_h6_shuf1: db 0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 5, 10, 6, 11,  7, 12
filter_h6_shuf2: db 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,  7, 7,  8,  8,  9
filter_h6_shuf3: db 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,  9, 9, 10, 10, 11

pw_20091: times 4 dw 20091
pw_17734: times 4 dw 17734

cextern pw_3
cextern pw_4
cextern pw_64

SECTION .text

;-----------------------------------------------------------------------------
; subpel MC functions:
;
; void put_vp8_epel<size>_h<htap>v<vtap>_<opt>(uint8_t *dst, int deststride,
;                                              uint8_t *src, int srcstride,
;                                              int height,   int mx, int my);
;-----------------------------------------------------------------------------

; 4x4 block, H-only 4-tap filter
cglobal put_vp8_epel4_h4_mmxext, 6, 6
    shl       r5d, 4
%ifdef PIC
    lea       r11, [fourtap_filter_hw_m]
%endif
    movq      mm4, [fourtap_filter_hw+r5-16] ; set up 4tap filter in words
    movq      mm5, [fourtap_filter_hw+r5]
    movq      mm7, [pw_64]
    pxor      mm6, mm6

.nextrow
    movq      mm1, [r2-1]                  ; (ABCDEFGH) load 8 horizontal pixels

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
    movd     [r0], mm3                     ; store

    ; go to next line
    add        r0, r1
    add        r2, r3
    dec        r4                          ; next row
    jg .nextrow
    REP_RET

; 4x4 block, H-only 6-tap filter
cglobal put_vp8_epel4_h6_mmxext, 6, 6
    lea       r5d, [r5*3]
%ifdef PIC
    lea       r11, [sixtap_filter_hw_m]
%endif
    movq      mm4, [sixtap_filter_hw+r5*8-48] ; set up 4tap filter in words
    movq      mm5, [sixtap_filter_hw+r5*8-32]
    movq      mm6, [sixtap_filter_hw+r5*8-16]
    movq      mm7, [pw_64]
    pxor      mm3, mm3

.nextrow
    movq      mm1, [r2-2]                  ; (ABCDEFGH) load 8 horizontal pixels

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
    movd      mm2, [r2+3]                  ; byte FGHI (prevent overreads)
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
    movd     [r0], mm1                     ; store

    ; go to next line
    add        r0, r1
    add        r2, r3
    dec        r4                          ; next row
    jg .nextrow
    REP_RET

; 4x4 block, H-only 4-tap filter
INIT_XMM
cglobal put_vp8_epel8_h4_sse2, 6, 6, 8
    shl      r5d, 4
%ifdef PIC
    lea      r11, [fourtap_filter_hw_m]
%endif
    mova      m5, [fourtap_filter_hw+r5-16] ; set up 4tap filter in words
    mova      m6, [fourtap_filter_hw+r5]
    pxor      m7, m7

.nextrow
    movh      m0, [r2-1]
    punpcklbw m0, m7        ; ABCDEFGH
    mova      m1, m0
    mova      m2, m0
    mova      m3, m0
    psrldq    m1, 2         ; BCDEFGH
    psrldq    m2, 4         ; CDEFGH
    psrldq    m3, 6         ; DEFGH
    punpcklwd m0, m1        ; ABBCCDDE
    punpcklwd m2, m3        ; CDDEEFFG
    pmaddwd   m0, m5
    pmaddwd   m2, m6
    paddd     m0, m2

    movh      m1, [r2+3]
    punpcklbw m1, m7        ; ABCDEFGH
    mova      m2, m1
    mova      m3, m1
    mova      m4, m1
    psrldq    m2, 2         ; BCDEFGH
    psrldq    m3, 4         ; CDEFGH
    psrldq    m4, 6         ; DEFGH
    punpcklwd m1, m2        ; ABBCCDDE
    punpcklwd m3, m4        ; CDDEEFFG
    pmaddwd   m1, m5
    pmaddwd   m3, m6
    paddd     m1, m3

    packssdw  m0, m1
    paddsw    m0, [pw_64]
    psraw     m0, 7
    packuswb  m0, m7
    movh    [r0], m0        ; store

    ; go to next line
    add       r0, r1
    add       r2, r3
    dec       r4            ; next row
    jg .nextrow
    REP_RET

cglobal put_vp8_epel8_h6_sse2, 6, 6, 8
    lea      r5d, [r5*3]
%ifdef PIC
    lea      r11, [sixtap_filter_hw_m]
%endif
    lea       r5, [sixtap_filter_hw+r5*8]
    pxor      m7, m7

.nextrow
    movu      m0, [r2-2]
    mova      m6, m0
    mova      m4, m0
    punpcklbw m0, m7        ; ABCDEFGHI
    mova      m1, m0
    mova      m2, m0
    mova      m3, m0
    psrldq    m1, 2         ; BCDEFGH
    psrldq    m2, 4         ; CDEFGH
    psrldq    m3, 6         ; DEFGH
    psrldq    m4, 4
    punpcklbw m4, m7        ; EFGH
    mova      m5, m4
    psrldq    m5, 2         ; FGH
    punpcklwd m0, m1        ; ABBCCDDE
    punpcklwd m2, m3        ; CDDEEFFG
    punpcklwd m4, m5        ; EFFGGHHI
    pmaddwd   m0, [r5-48]
    pmaddwd   m2, [r5-32]
    pmaddwd   m4, [r5-16]
    paddd     m0, m2
    paddd     m0, m4

    psrldq    m6, 4
    mova      m4, m6
    punpcklbw m6, m7        ; ABCDEFGHI
    mova      m1, m6
    mova      m2, m6
    mova      m3, m6
    psrldq    m1, 2         ; BCDEFGH
    psrldq    m2, 4         ; CDEFGH
    psrldq    m3, 6         ; DEFGH
    psrldq    m4, 4
    punpcklbw m4, m7        ; EFGH
    mova      m5, m4
    psrldq    m5, 2         ; FGH
    punpcklwd m6, m1        ; ABBCCDDE
    punpcklwd m2, m3        ; CDDEEFFG
    punpcklwd m4, m5        ; EFFGGHHI
    pmaddwd   m6, [r5-48]
    pmaddwd   m2, [r5-32]
    pmaddwd   m4, [r5-16]
    paddd     m6, m2
    paddd     m6, m4

    packssdw  m0, m6
    paddsw    m0, [pw_64]
    psraw     m0, 7
    packuswb  m0, m7
    movh    [r0], m0        ; store

    ; go to next line
    add       r0, r1
    add       r2, r3
    dec       r4            ; next row
    jg .nextrow
    REP_RET

cglobal put_vp8_epel8_h4_ssse3, 6, 6, 7
    shl      r5d, 4
    mova      m2, [pw_64]
    mova      m3, [filter_h4_shuf]
    mova      m4, [filter_h6_shuf2]
%ifdef PIC
    lea      r11, [fourtap_filter_hb_m]
%endif
    mova      m5, [fourtap_filter_hb+r5-16] ; set up 4tap filter in bytes
    mova      m6, [fourtap_filter_hb+r5]

.nextrow
    movu      m0, [r2-1]
    mova      m1, m0
    pshufb    m0, m3
    pshufb    m1, m4
    pmaddubsw m0, m5
    pmaddubsw m1, m6
    paddsw    m0, m2
    paddsw    m0, m1
    psraw     m0, 7
    packuswb  m0, m0
    movh    [r0], m0        ; store

    ; go to next line
    add       r0, r1
    add       r2, r3
    dec       r4            ; next row
    jg .nextrow
    REP_RET

cglobal put_vp8_epel8_h6_ssse3, 6, 6, 8
    lea      r5d, [r5*3]
    mova      m3, [filter_h6_shuf1]
    mova      m4, [filter_h6_shuf2]
%ifdef PIC
    lea      r11, [sixtap_filter_hb_m]
%endif
    mova      m5, [sixtap_filter_hb+r5*8-48] ; set up 6tap filter in bytes
    mova      m6, [sixtap_filter_hb+r5*8-32]
    mova      m7, [sixtap_filter_hb+r5*8-16]

.nextrow
    movu      m0, [r2-2]
    mova      m1, m0
    mova      m2, m0
    pshufb    m0, m3
    pshufb    m1, m4
    pshufb    m2, [filter_h6_shuf3]
    pmaddubsw m0, m5
    pmaddubsw m1, m6
    pmaddubsw m2, m7
    paddsw    m0, m1
    paddsw    m0, m2
    paddsw    m0, [pw_64]
    psraw     m0, 7
    packuswb  m0, m0
    movh    [r0], m0        ; store

    ; go to next line
    add       r0, r1
    add       r2, r3
    dec       r4            ; next row
    jg .nextrow
    REP_RET

%macro FILTER_V 3
; 4x4 block, V-only 4-tap filter
cglobal put_vp8_epel%2_v4_%1, 7, 7, %3
    shl      r6d, 5
%ifdef PIC
    lea      r11, [fourtap_filter_v_m]
%endif
    lea       r6, [fourtap_filter_v+r6-32]
    mova      m6, [pw_64]
    pxor      m7, m7
    mova      m5, [r6+48]

    ; read 3 lines
    sub       r2, r3
    movh      m0, [r2]
    movh      m1, [r2+  r3]
    movh      m2, [r2+2*r3]
    add       r2, r3
    punpcklbw m0, m7
    punpcklbw m1, m7
    punpcklbw m2, m7

.nextrow
    ; first calculate negative taps (to prevent losing positive overflows)
    movh      m4, [r2+2*r3]                ; read new row
    punpcklbw m4, m7
    mova      m3, m4
    pmullw    m0, [r6+0]
    pmullw    m4, m5
    paddsw    m4, m0

    ; then calculate positive taps
    mova      m0, m1
    pmullw    m1, [r6+16]
    paddsw    m4, m1
    mova      m1, m2
    pmullw    m2, [r6+32]
    paddsw    m4, m2
    mova      m2, m3

    ; round/clip/store
    paddsw    m4, m6
    psraw     m4, 7
    packuswb  m4, m7
    movh    [r0], m4

    ; go to next line
    add       r0, r1
    add       r2, r3
    dec       r4                           ; next row
    jg .nextrow
    REP_RET


; 4x4 block, V-only 6-tap filter
cglobal put_vp8_epel%2_v6_%1, 7, 7, %3
    shl      r6d, 4
    lea       r6, [r6*3]
%ifdef PIC
    lea      r11, [sixtap_filter_v_m]
%endif
    lea       r6, [sixtap_filter_v+r6-96]
    pxor      m7, m7

    ; read 5 lines
    sub       r2, r3
    sub       r2, r3
    movh      m0, [r2]
    movh      m1, [r2+r3]
    movh      m2, [r2+r3*2]
    lea       r2, [r2+r3*2]
    add       r2, r3
    movh      m3, [r2]
    movh      m4, [r2+r3]
    punpcklbw m0, m7
    punpcklbw m1, m7
    punpcklbw m2, m7
    punpcklbw m3, m7
    punpcklbw m4, m7

.nextrow
    ; first calculate negative taps (to prevent losing positive overflows)
    mova      m5, m1
    pmullw    m5, [r6+16]
    mova      m6, m4
    pmullw    m6, [r6+64]
    paddsw    m6, m5

    ; then calculate positive taps
    movh      m5, [r2+2*r3]                ; read new row
    punpcklbw m5, m7
    pmullw    m0, [r6+0]
    paddsw    m6, m0
    mova      m0, m1
    mova      m1, m2
    pmullw    m2, [r6+32]
    paddsw    m6, m2
    mova      m2, m3
    pmullw    m3, [r6+48]
    paddsw    m6, m3
    mova      m3, m4
    mova      m4, m5
    pmullw    m5, [r6+80]
    paddsw    m6, m5

    ; round/clip/store
    paddsw    m6, [pw_64]
    psraw     m6, 7
    packuswb  m6, m7
    movh    [r0], m6

    ; go to next line
    add       r0, r1
    add       r2, r3
    dec       r4                           ; next row
    jg .nextrow
    REP_RET
%endmacro

INIT_MMX
FILTER_V mmxext, 4, 0
INIT_XMM
FILTER_V sse2,   8, 8

cglobal put_vp8_epel8_v4_ssse3, 7, 7, 8
    shl      r6d, 4
%ifdef PIC
    lea      r11, [fourtap_filter_hb_m]
%endif
    mova      m5, [fourtap_filter_hb+r6-16]
    mova      m6, [fourtap_filter_hb+r6]
    mova      m7, [pw_64]

    ; read 3 lines
    sub       r2, r3
    movh      m0, [r2]
    movh      m1, [r2+  r3]
    movh      m2, [r2+2*r3]
    add       r2, r3

.nextrow
    movh      m3, [r2+2*r3]                ; read new row
    mova      m4, m0
    mova      m0, m1
    punpcklbw m4, m3
    punpcklbw m1, m2
    pmaddubsw m4, m5
    pmaddubsw m1, m6
    paddsw    m4, m1
    mova      m1, m2
    paddsw    m4, m7
    mova      m2, m3
    psraw     m4, 7
    packuswb  m4, m4
    movh    [r0], m4

    ; go to next line
    add        r0, r1
    add        r2, r3
    dec        r4                          ; next row
    jg .nextrow
    REP_RET

cglobal put_vp8_epel8_v6_ssse3, 7, 7, 8
    lea      r6d, [r6*3]
%ifdef PIC
    lea      r11, [sixtap_filter_hb_m]
%endif
    lea       r6, [sixtap_filter_hb+r6*8]

    ; read 5 lines
    sub       r2, r3
    sub       r2, r3
    movh      m0, [r2]
    movh      m1, [r2+r3]
    movh      m2, [r2+r3*2]
    lea       r2, [r2+r3*2]
    add       r2, r3
    movh      m3, [r2]
    movh      m4, [r2+r3]

.nextrow
    movh      m5, [r2+2*r3]                ; read new row
    mova      m6, m0
    punpcklbw m6, m5
    mova      m0, m1
    punpcklbw m1, m2
    mova      m7, m3
    punpcklbw m7, m4
    pmaddubsw m6, [r6-48]
    pmaddubsw m1, [r6-32]
    pmaddubsw m7, [r6-16]
    paddsw    m6, m1
    paddsw    m6, m7
    mova      m1, m2
    paddsw    m6, [pw_64]
    mova      m2, m3
    psraw     m6, 7
    mova      m3, m4
    packuswb  m6, m6
    mova      m4, m5
    movh    [r0], m6

    ; go to next line
    add        r0, r1
    add        r2, r3
    dec        r4                          ; next row
    jg .nextrow
    REP_RET

%macro FILTER_BILINEAR 3
cglobal put_vp8_bilinear%2_v_%1, 7,7,%3
    mov      r5d, 8*16
    shl      r6d, 4
    sub      r5d, r6d
%ifdef PIC
    lea      r11, [bilinear_filter_vw_m]
%endif
    pxor      m6, m6
    mova      m4, [bilinear_filter_vw+r5-16]
    mova      m5, [bilinear_filter_vw+r6-16]
.nextrow
    movh      m0, [r2+r3*0]
    movh      m1, [r2+r3*1]
    movh      m3, [r2+r3*2]
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
%ifidn %1, mmxext
    packuswb  m0, m0
    packuswb  m2, m2
    movh [r0+r1*0], m0
    movh [r0+r1*1], m2
%else
    packuswb  m0, m2
    movh   [r0+r1*0], m0
    movhps [r0+r1*1], m0
%endif

    lea       r0, [r0+r1*2]
    lea       r2, [r2+r3*2]
    sub       r4, 2
    jg .nextrow
    REP_RET

cglobal put_vp8_bilinear%2_h_%1, 7,7,%3
    mov      r6d, 8*16
    shl      r5d, 4
    sub      r6d, r5d
%ifdef PIC
    lea      r11, [bilinear_filter_vw_m]
%endif
    pxor      m6, m6
    mova      m4, [bilinear_filter_vw+r6-16]
    mova      m5, [bilinear_filter_vw+r5-16]
.nextrow
    movh      m0, [r2+r3*0+0]
    movh      m1, [r2+r3*0+1]
    movh      m2, [r2+r3*1+0]
    movh      m3, [r2+r3*1+1]
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
%ifidn %1, mmxext
    packuswb  m0, m0
    packuswb  m2, m2
    movh [r0+r1*0], m0
    movh [r0+r1*1], m2
%else
    packuswb  m0, m2
    movh   [r0+r1*0], m0
    movhps [r0+r1*1], m0
%endif

    lea       r0, [r0+r1*2]
    lea       r2, [r2+r3*2]
    sub       r4, 2
    jg .nextrow
    REP_RET
%endmacro

INIT_MMX
FILTER_BILINEAR mmxext, 4, 0
INIT_XMM
FILTER_BILINEAR   sse2, 8, 7

cglobal put_vp8_bilinear8_v_ssse3, 7,7,5
    shl      r6d, 4
%ifdef PIC
    lea      r11, [bilinear_filter_vb_m]
%endif
    pxor      m4, m4
    mova      m3, [bilinear_filter_vb+r6-16]
.nextrow
    movh      m0, [r2+r3*0]
    movh      m1, [r2+r3*1]
    movh      m2, [r2+r3*2]
    punpcklbw m0, m1
    punpcklbw m1, m2
    pmaddubsw m0, m3
    pmaddubsw m1, m3
    psraw     m0, 2
    psraw     m1, 2
    pavgw     m0, m4
    pavgw     m1, m4
    packuswb  m0, m1
    movh   [r0+r1*0], m0
    movhps [r0+r1*1], m0

    lea       r0, [r0+r1*2]
    lea       r2, [r2+r3*2]
    sub       r4, 2
    jg .nextrow
    REP_RET

cglobal put_vp8_bilinear8_h_ssse3, 7,7,5
    shl      r5d, 4
%ifdef PIC
    lea      r11, [bilinear_filter_vb_m]
%endif
    pxor      m4, m4
    mova      m2, [filter_h2_shuf]
    mova      m3, [bilinear_filter_vb+r5-16]
.nextrow
    movu      m0, [r2+r3*0]
    movu      m1, [r2+r3*1]
    pshufb    m0, m2
    pshufb    m1, m2
    pmaddubsw m0, m3
    pmaddubsw m1, m3
    psraw     m0, 2
    psraw     m1, 2
    pavgw     m0, m4
    pavgw     m1, m4
    packuswb  m0, m1
    movh   [r0+r1*0], m0
    movhps [r0+r1*1], m0

    lea       r0, [r0+r1*2]
    lea       r2, [r2+r3*2]
    sub       r4, 2
    jg .nextrow
    REP_RET

cglobal put_vp8_pixels8_mmx, 5,5
.nextrow:
    movq  mm0, [r2+r3*0]
    movq  mm1, [r2+r3*1]
    lea    r2, [r2+r3*2]
    movq [r0+r1*0], mm0
    movq [r0+r1*1], mm1
    lea    r0, [r0+r1*2]
    sub   r4d, 2
    jg .nextrow
    REP_RET

cglobal put_vp8_pixels16_mmx, 5,5
.nextrow:
    movq  mm0, [r2+r3*0+0]
    movq  mm1, [r2+r3*0+8]
    movq  mm2, [r2+r3*1+0]
    movq  mm3, [r2+r3*1+8]
    lea    r2, [r2+r3*2]
    movq [r0+r1*0+0], mm0
    movq [r0+r1*0+8], mm1
    movq [r0+r1*1+0], mm2
    movq [r0+r1*1+8], mm3
    lea    r0, [r0+r1*2]
    sub   r4d, 2
    jg .nextrow
    REP_RET

cglobal put_vp8_pixels16_sse, 5,5,2
.nextrow:
    movups xmm0, [r2+r3*0]
    movups xmm1, [r2+r3*1]
    lea     r2, [r2+r3*2]
    movaps [r0+r1*0], xmm0
    movaps [r0+r1*1], xmm1
    lea     r0, [r0+r1*2]
    sub    r4d, 2
    jg .nextrow
    REP_RET

;-----------------------------------------------------------------------------
; IDCT functions:
;
; void vp8_idct_dc_add_<opt>(uint8_t *dst, DCTELEM block[16], int stride);
;-----------------------------------------------------------------------------

cglobal vp8_idct_dc_add_mmx, 3, 3
    ; load data
    movd       mm0, [r1]

    ; calculate DC
    paddw      mm0, [pw_4]
    pxor       mm1, mm1
    psraw      mm0, 3
    psubw      mm1, mm0
    packuswb   mm0, mm0
    packuswb   mm1, mm1
    punpcklbw  mm0, mm0
    punpcklbw  mm1, mm1
    punpcklwd  mm0, mm0
    punpcklwd  mm1, mm1

    ; add DC
    lea         r1, [r0+r2*2]
    movd       mm2, [r0]
    movd       mm3, [r0+r2]
    movd       mm4, [r1]
    movd       mm5, [r1+r2]
    paddusb    mm2, mm0
    paddusb    mm3, mm0
    paddusb    mm4, mm0
    paddusb    mm5, mm0
    psubusb    mm2, mm1
    psubusb    mm3, mm1
    psubusb    mm4, mm1
    psubusb    mm5, mm1
    movd      [r0], mm2
    movd   [r0+r2], mm3
    movd      [r1], mm4
    movd   [r1+r2], mm5
    RET

cglobal vp8_idct_dc_add_sse4, 3, 3, 6
    ; load data
    movd       xmm0, [r1]
    lea          r1, [r0+r2*2]
    pxor       xmm1, xmm1
    movq       xmm2, [pw_4]

    ; calculate DC
    paddw      xmm0, xmm2
    movd       xmm2, [r0]
    movd       xmm3, [r0+r2]
    movd       xmm4, [r1]
    movd       xmm5, [r1+r2]
    psraw      xmm0, 3
    pshuflw    xmm0, xmm0, 0
    punpcklqdq xmm0, xmm0
    punpckldq  xmm2, xmm3
    punpckldq  xmm4, xmm5
    punpcklbw  xmm2, xmm1
    punpcklbw  xmm4, xmm1
    paddw      xmm2, xmm0
    paddw      xmm4, xmm0
    packuswb   xmm2, xmm4
    movd       [r0], xmm2
    pextrd  [r0+r2], xmm2, 1
    pextrd     [r1], xmm2, 2
    pextrd  [r1+r2], xmm2, 3
    RET

;-----------------------------------------------------------------------------
; void vp8_idct_add_<opt>(uint8_t *dst, DCTELEM block[16], int stride);
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
    SUMSUB_BA           m%3, m%1, m%5     ;t0, t1
    VP8_MULTIPLY_SUMSUB m%2, m%4, m%5,m%6 ;t2, t3
    SUMSUB_BA           m%4, m%3, m%5     ;tmp0, tmp3
    SUMSUB_BA           m%2, m%1, m%5     ;tmp1, tmp2
    SWAP                 %4,  %1
    SWAP                 %4,  %3
%endmacro

INIT_MMX
cglobal vp8_idct_add_mmx, 3, 3
    ; load block data
    movq         m0, [r1]
    movq         m1, [r1+8]
    movq         m2, [r1+16]
    movq         m3, [r1+24]
    movq         m6, [pw_20091]
    movq         m7, [pw_17734]

    ; actual IDCT
    VP8_IDCT_TRANSFORM4x4_1D 0, 1, 2, 3, 4, 5
    TRANSPOSE4x4W            0, 1, 2, 3, 4
    paddw        m0, [pw_4]
    VP8_IDCT_TRANSFORM4x4_1D 0, 1, 2, 3, 4, 5
    TRANSPOSE4x4W            0, 1, 2, 3, 4

    ; store
    pxor         m4, m4
    lea          r1, [r0+2*r2]
    STORE_DIFFx2 m0, m1, m6, m7, m4, 3, r0, r2
    STORE_DIFFx2 m2, m3, m6, m7, m4, 3, r1, r2

    RET

;-----------------------------------------------------------------------------
; void vp8_luma_dc_wht_mmxext(DCTELEM block[4][4][16], DCTELEM dc[16])
;-----------------------------------------------------------------------------

%macro SCATTER_WHT 1
    pextrw r1d, m0, %1
    pextrw r2d, m1, %1
    mov [r0+2*16*0], r1w
    mov [r0+2*16*1], r2w
    pextrw r1d, m2, %1
    pextrw r2d, m3, %1
    mov [r0+2*16*2], r1w
    mov [r0+2*16*3], r2w
%endmacro

%macro HADAMARD4_1D 4
    SUMSUB_BADC m%2, m%1, m%4, m%3
    SUMSUB_BADC m%4, m%2, m%3, m%1
    SWAP %1, %4, %3
%endmacro

INIT_MMX
cglobal vp8_luma_dc_wht_mmxext, 2,3
    movq          m0, [r1]
    movq          m1, [r1+8]
    movq          m2, [r1+16]
    movq          m3, [r1+24]
    HADAMARD4_1D  0, 1, 2, 3
    TRANSPOSE4x4W 0, 1, 2, 3, 4
    paddw         m0, [pw_3]
    HADAMARD4_1D  0, 1, 2, 3
    psraw         m0, 3
    psraw         m1, 3
    psraw         m2, 3
    psraw         m3, 3
    SCATTER_WHT   0
    add           r0, 2*16*4
    SCATTER_WHT   1
    add           r0, 2*16*4
    SCATTER_WHT   2
    add           r0, 2*16*4
    SCATTER_WHT   3
    RET
