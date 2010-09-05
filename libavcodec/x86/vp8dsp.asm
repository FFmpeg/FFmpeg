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
filter_h4_shuf:  db 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,  8, 8,  9,  9, 10

filter_h6_shuf1: db 0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 5, 10, 6, 11,  7, 12
filter_h6_shuf2: db 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,  7, 7,  8,  8,  9
filter_h6_shuf3: db 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,  9, 9, 10, 10, 11

pw_20091: times 4 dw 20091
pw_17734: times 4 dw 17734

pb_27_63: times 8 db 27, 63
pb_18_63: times 8 db 18, 63
pb_9_63:  times 8 db  9, 63

cextern pb_1
cextern pw_3
cextern pb_3
cextern pw_4
cextern pb_4
cextern pw_9
cextern pw_18
cextern pw_27
cextern pw_63
cextern pw_64
cextern pb_80
cextern pb_F8
cextern pb_FE

SECTION .text

;-----------------------------------------------------------------------------
; subpel MC functions:
;
; void put_vp8_epel<size>_h<htap>v<vtap>_<opt>(uint8_t *dst, int deststride,
;                                              uint8_t *src, int srcstride,
;                                              int height,   int mx, int my);
;-----------------------------------------------------------------------------

%macro FILTER_SSSE3 3
cglobal put_vp8_epel%1_h6_ssse3, 6, 6, %2
    lea      r5d, [r5*3]
    mova      m3, [filter_h6_shuf2]
    mova      m4, [filter_h6_shuf3]
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
%ifidn %1, 4
; For epel4, we need 9 bytes, but only 8 get loaded; to compensate, do the
; shuffle with a memory operand
    punpcklbw m0, [r2+3]
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
    paddsw    m0, [pw_64]
    psraw     m0, 7
    packuswb  m0, m0
    movh    [r0], m0        ; store

    ; go to next line
    add       r0, r1
    add       r2, r3
    dec      r4d            ; next row
    jg .nextrow
    REP_RET

cglobal put_vp8_epel%1_h4_ssse3, 6, 6, %3
    shl      r5d, 4
    mova      m2, [pw_64]
    mova      m3, [filter_h2_shuf]
    mova      m4, [filter_h4_shuf]
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
    dec      r4d            ; next row
    jg .nextrow
    REP_RET

cglobal put_vp8_epel%1_v4_ssse3, 7, 7, %2
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
    punpcklbw m4, m1
    mova      m1, m2
    punpcklbw m2, m3
    pmaddubsw m4, m5
    pmaddubsw m2, m6
    paddsw    m4, m2
    mova      m2, m3
    paddsw    m4, m7
    psraw     m4, 7
    packuswb  m4, m4
    movh    [r0], m4

    ; go to next line
    add        r0, r1
    add        r2, r3
    dec       r4d                          ; next row
    jg .nextrow
    REP_RET

cglobal put_vp8_epel%1_v6_ssse3, 7, 7, %2
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
    dec       r4d                          ; next row
    jg .nextrow
    REP_RET
%endmacro

INIT_MMX
FILTER_SSSE3 4, 0, 0
INIT_XMM
FILTER_SSSE3 8, 8, 7

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
    dec       r4d                          ; next row
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
    dec       r4d                          ; next row
    jg .nextrow
    REP_RET

INIT_XMM
cglobal put_vp8_epel8_h4_sse2, 6, 6, 10
    shl      r5d, 5
%ifdef PIC
    lea      r11, [fourtap_filter_v_m]
%endif
    lea       r5, [fourtap_filter_v+r5-32]
    pxor      m7, m7
    mova      m4, [pw_64]
    mova      m5, [r5+ 0]
    mova      m6, [r5+16]
%ifdef m8
    mova      m8, [r5+32]
    mova      m9, [r5+48]
%endif
.nextrow
    movq      m0, [r2-1]
    movq      m1, [r2-0]
    movq      m2, [r2+1]
    movq      m3, [r2+2]
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
    pmullw    m2, [r5+32]
    pmullw    m3, [r5+48]
%endif
    paddsw    m0, m1
    paddsw    m2, m3
    paddsw    m0, m2
    paddsw    m0, m4
    psraw     m0, 7
    packuswb  m0, m7
    movh    [r0], m0        ; store

    ; go to next line
    add       r0, r1
    add       r2, r3
    dec      r4d            ; next row
    jg .nextrow
    REP_RET

cglobal put_vp8_epel8_h6_sse2, 6, 6, 14
    lea      r5d, [r5*3]
    shl      r5d, 4
%ifdef PIC
    lea      r11, [sixtap_filter_v_m]
%endif
    lea       r5, [sixtap_filter_v+r5-96]
    pxor      m7, m7
    mova      m6, [pw_64]
%ifdef m8
    mova      m8, [r5+ 0]
    mova      m9, [r5+16]
    mova     m10, [r5+32]
    mova     m11, [r5+48]
    mova     m12, [r5+64]
    mova     m13, [r5+80]
%endif
.nextrow
    movq      m0, [r2-2]
    movq      m1, [r2-1]
    movq      m2, [r2-0]
    movq      m3, [r2+1]
    movq      m4, [r2+2]
    movq      m5, [r2+3]
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
    pmullw    m0, [r5+ 0]
    pmullw    m1, [r5+16]
    pmullw    m2, [r5+32]
    pmullw    m3, [r5+48]
    pmullw    m4, [r5+64]
    pmullw    m5, [r5+80]
%endif
    paddsw    m1, m4
    paddsw    m0, m5
    paddsw    m1, m2
    paddsw    m0, m3
    paddsw    m0, m1
    paddsw    m0, m6
    psraw     m0, 7
    packuswb  m0, m7
    movh    [r0], m0        ; store

    ; go to next line
    add       r0, r1
    add       r2, r3
    dec      r4d            ; next row
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
    dec      r4d                           ; next row
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
    dec      r4d                           ; next row
    jg .nextrow
    REP_RET
%endmacro

INIT_MMX
FILTER_V mmxext, 4, 0
INIT_XMM
FILTER_V sse2,   8, 8

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
    sub      r4d, 2
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
    sub      r4d, 2
    jg .nextrow
    REP_RET
%endmacro

INIT_MMX
FILTER_BILINEAR mmxext, 4, 0
INIT_XMM
FILTER_BILINEAR   sse2, 8, 7

%macro FILTER_BILINEAR_SSSE3 1
cglobal put_vp8_bilinear%1_v_ssse3, 7,7
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
%if mmsize==8
    packuswb  m0, m0
    packuswb  m1, m1
    movh [r0+r1*0], m0
    movh [r0+r1*1], m1
%else
    packuswb  m0, m1
    movh   [r0+r1*0], m0
    movhps [r0+r1*1], m0
%endif

    lea       r0, [r0+r1*2]
    lea       r2, [r2+r3*2]
    sub      r4d, 2
    jg .nextrow
    REP_RET

cglobal put_vp8_bilinear%1_h_ssse3, 7,7
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
%if mmsize==8
    packuswb  m0, m0
    packuswb  m1, m1
    movh [r0+r1*0], m0
    movh [r0+r1*1], m1
%else
    packuswb  m0, m1
    movh   [r0+r1*0], m0
    movhps [r0+r1*1], m0
%endif

    lea       r0, [r0+r1*2]
    lea       r2, [r2+r3*2]
    sub      r4d, 2
    jg .nextrow
    REP_RET
%endmacro

INIT_MMX
FILTER_BILINEAR_SSSE3 4
INIT_XMM
FILTER_BILINEAR_SSSE3 8

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
; void vp8_idct_dc_add_<opt>(uint8_t *dst, DCTELEM block[16], int stride);
;-----------------------------------------------------------------------------

%macro ADD_DC 4
    %4        m2, [r0+%3]
    %4        m3, [r0+r2+%3]
    %4        m4, [r1+%3]
    %4        m5, [r1+r2+%3]
    paddusb   m2, %1
    paddusb   m3, %1
    paddusb   m4, %1
    paddusb   m5, %1
    psubusb   m2, %2
    psubusb   m3, %2
    psubusb   m4, %2
    psubusb   m5, %2
    %4    [r0+%3], m2
    %4 [r0+r2+%3], m3
    %4    [r1+%3], m4
    %4 [r1+r2+%3], m5
%endmacro

INIT_MMX
cglobal vp8_idct_dc_add_mmx, 3, 3
    ; load data
    movd       m0, [r1]

    ; calculate DC
    paddw      m0, [pw_4]
    pxor       m1, m1
    psraw      m0, 3
    movd      [r1], m1
    psubw      m1, m0
    packuswb   m0, m0
    packuswb   m1, m1
    punpcklbw  m0, m0
    punpcklbw  m1, m1
    punpcklwd  m0, m0
    punpcklwd  m1, m1

    ; add DC
    lea        r1, [r0+r2*2]
    ADD_DC     m0, m1, 0, movh
    RET

INIT_XMM
cglobal vp8_idct_dc_add_sse4, 3, 3, 6
    ; load data
    movd       m0, [r1]
    pxor       m1, m1

    ; calculate DC
    paddw      m0, [pw_4]
    movd     [r1], m1
    lea        r1, [r0+r2*2]
    movd       m2, [r0]
    movd       m3, [r0+r2]
    movd       m4, [r1]
    movd       m5, [r1+r2]
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
    movd      [r0], m2
    pextrd [r0+r2], m2, 1
    pextrd    [r1], m2, 2
    pextrd [r1+r2], m2, 3
    RET

;-----------------------------------------------------------------------------
; void vp8_idct_dc_add4y_<opt>(uint8_t *dst, DCTELEM block[4][16], int stride);
;-----------------------------------------------------------------------------

INIT_MMX
cglobal vp8_idct_dc_add4y_mmx, 3, 3
    ; load data
    movd      m0, [r1+32*0] ; A
    movd      m1, [r1+32*2] ; C
    punpcklwd m0, [r1+32*1] ; A B
    punpcklwd m1, [r1+32*3] ; C D
    punpckldq m0, m1        ; A B C D
    pxor      m6, m6

    ; calculate DC
    paddw     m0, [pw_4]
    movd [r1+32*0], m6
    movd [r1+32*1], m6
    movd [r1+32*2], m6
    movd [r1+32*3], m6
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
    lea       r1, [r0+r2*2]
    ADD_DC    m0, m6, 0, mova
    ADD_DC    m1, m7, 8, mova
    RET

INIT_XMM
cglobal vp8_idct_dc_add4y_sse2, 3, 3, 6
    ; load data
    movd      m0, [r1+32*0] ; A
    movd      m1, [r1+32*2] ; C
    punpcklwd m0, [r1+32*1] ; A B
    punpcklwd m1, [r1+32*3] ; C D
    punpckldq m0, m1        ; A B C D
    pxor      m1, m1

    ; calculate DC
    paddw     m0, [pw_4]
    movd [r1+32*0], m1
    movd [r1+32*1], m1
    movd [r1+32*2], m1
    movd [r1+32*3], m1
    psraw     m0, 3
    psubw     m1, m0
    packuswb  m0, m0
    packuswb  m1, m1
    punpcklbw m0, m0
    punpcklbw m1, m1
    punpcklbw m0, m0
    punpcklbw m1, m1

    ; add DC
    lea       r1, [r0+r2*2]
    ADD_DC    m0, m1, 0, mova
    RET

;-----------------------------------------------------------------------------
; void vp8_idct_dc_add4uv_<opt>(uint8_t *dst, DCTELEM block[4][16], int stride);
;-----------------------------------------------------------------------------

INIT_MMX
cglobal vp8_idct_dc_add4uv_mmx, 3, 3
    ; load data
    movd      m0, [r1+32*0] ; A
    movd      m1, [r1+32*2] ; C
    punpcklwd m0, [r1+32*1] ; A B
    punpcklwd m1, [r1+32*3] ; C D
    punpckldq m0, m1        ; A B C D
    pxor      m6, m6

    ; calculate DC
    paddw     m0, [pw_4]
    movd [r1+32*0], m6
    movd [r1+32*1], m6
    movd [r1+32*2], m6
    movd [r1+32*3], m6
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
    lea       r1, [r0+r2*2]
    ADD_DC    m0, m6, 0, mova
    lea       r0, [r0+r2*4]
    lea       r1, [r1+r2*4]
    ADD_DC    m1, m7, 0, mova
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
%macro VP8_IDCT_ADD 1
cglobal vp8_idct_add_%1, 3, 3
    ; load block data
    movq         m0, [r1+ 0]
    movq         m1, [r1+ 8]
    movq         m2, [r1+16]
    movq         m3, [r1+24]
    movq         m6, [pw_20091]
    movq         m7, [pw_17734]
%ifidn %1, sse
    xorps      xmm0, xmm0
    movaps  [r1+ 0], xmm0
    movaps  [r1+16], xmm0
%else
    pxor         m4, m4
    movq    [r1+ 0], m4
    movq    [r1+ 8], m4
    movq    [r1+16], m4
    movq    [r1+24], m4
%endif

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
%endmacro

VP8_IDCT_ADD mmx
VP8_IDCT_ADD sse

;-----------------------------------------------------------------------------
; void vp8_luma_dc_wht_mmxext(DCTELEM block[4][4][16], DCTELEM dc[16])
;-----------------------------------------------------------------------------

%macro SCATTER_WHT 3
    movd  r1d, m%1
    movd  r2d, m%2
    mov [r0+2*16*(0+%3)], r1w
    mov [r0+2*16*(1+%3)], r2w
    shr   r1d, 16
    shr   r2d, 16
    psrlq m%1, 32
    psrlq m%2, 32
    mov [r0+2*16*(4+%3)], r1w
    mov [r0+2*16*(5+%3)], r2w
    movd  r1d, m%1
    movd  r2d, m%2
    mov [r0+2*16*(8+%3)], r1w
    mov [r0+2*16*(9+%3)], r2w
    shr   r1d, 16
    shr   r2d, 16
    mov [r0+2*16*(12+%3)], r1w
    mov [r0+2*16*(13+%3)], r2w
%endmacro

%macro HADAMARD4_1D 4
    SUMSUB_BADC m%2, m%1, m%4, m%3
    SUMSUB_BADC m%4, m%2, m%3, m%1
    SWAP %1, %4, %3
%endmacro

%macro VP8_DC_WHT 1
cglobal vp8_luma_dc_wht_%1, 2,3
    movq          m0, [r1]
    movq          m1, [r1+8]
    movq          m2, [r1+16]
    movq          m3, [r1+24]
%ifidn %1, sse
    xorps      xmm0, xmm0
    movaps  [r1+ 0], xmm0
    movaps  [r1+16], xmm0
%else
    pxor         m4, m4
    movq    [r1+ 0], m4
    movq    [r1+ 8], m4
    movq    [r1+16], m4
    movq    [r1+24], m4
%endif
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
%endmacro

INIT_MMX
VP8_DC_WHT mmx
VP8_DC_WHT sse

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

%macro WRITE_8W_SSE2 5
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
%endmacro

%macro WRITE_8W_SSE4 5
    pextrw    [%3+%4*4], %1, 0
    pextrw    [%2+%4*4], %1, 1
    pextrw    [%3+%4*2], %1, 2
    pextrw    [%3+%4  ], %1, 3
    pextrw    [%3     ], %1, 4
    pextrw    [%2     ], %1, 5
    pextrw    [%2+%5  ], %1, 6
    pextrw    [%2+%5*2], %1, 7
%endmacro

%macro SPLATB_REG_MMX 2-3
    movd           %1, %2d
    punpcklbw      %1, %1
    punpcklwd      %1, %1
    punpckldq      %1, %1
%endmacro

%macro SPLATB_REG_MMXEXT 2-3
    movd           %1, %2d
    punpcklbw      %1, %1
    pshufw         %1, %1, 0x0
%endmacro

%macro SPLATB_REG_SSE2 2-3
    movd           %1, %2d
    punpcklbw      %1, %1
    pshuflw        %1, %1, 0x0
    punpcklqdq     %1, %1
%endmacro

%macro SPLATB_REG_SSSE3 3
    movd           %1, %2d
    pshufb         %1, %3
%endmacro

%macro SIMPLE_LOOPFILTER 4
cglobal vp8_%2_loop_filter_simple_%1, 3, %3, %4
%if mmsize == 8 ; mmx/mmxext
    mov            r3, 2
%endif
%ifnidn %1, sse2
%if mmsize == 16
    pxor           m0, m0
%endif
%endif
    SPLATB_REG     m7, r2, m0       ; splat "flim" into register

    ; set up indexes to address 4 rows
    mov            r2, r1
    neg            r1
%ifidn %2, h
    lea            r0, [r0+4*r2-2]
%endif

%if mmsize == 8 ; mmx / mmxext
.next8px
%endif
%ifidn %2, v
    ; read 4 half/full rows of pixels
    mova           m0, [r0+r1*2]    ; p1
    mova           m1, [r0+r1]      ; p0
    mova           m2, [r0]         ; q0
    mova           m3, [r0+r2]      ; q1
%else ; h
    lea            r4, [r0+r2]

%if mmsize == 8 ; mmx/mmxext
    READ_8x4_INTERLEAVED  0, 1, 2, 3, 4, 5, 6, r0, r4, r1, r2
%else ; sse2
    READ_16x4_INTERLEAVED 0, 1, 2, 3, 4, 5, 6, r0, r4, r1, r2, r3
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
%ifidn %2, v
    mova         [r0], m4
    mova      [r0+r1], m6
%else ; h
    inc           r0
    SBUTTERFLY    bw, 6, 4, 0

%if mmsize == 16 ; sse2
%ifidn %1, sse4
    inc            r4
%endif
    WRITE_8W       m6, r4, r0, r1, r2
    lea            r4, [r3+r1+1]
%ifidn %1, sse4
    inc            r3
%endif
    WRITE_8W       m4, r3, r4, r1, r2
%else ; mmx/mmxext
    WRITE_2x4W     m6, m4, r4, r0, r1, r2
%endif
%endif

%if mmsize == 8 ; mmx/mmxext
    ; next 8 pixels
%ifidn %2, v
    add            r0, 8            ; advance 8 cols = pixels
%else ; h
    lea            r0, [r0+r2*8-1]  ; advance 8 rows = lines
%endif
    dec            r3
    jg .next8px
    REP_RET
%else ; sse2
    RET
%endif
%endmacro

INIT_MMX
%define SPLATB_REG SPLATB_REG_MMX
SIMPLE_LOOPFILTER mmx,    v, 4, 0
SIMPLE_LOOPFILTER mmx,    h, 5, 0
%define SPLATB_REG SPLATB_REG_MMXEXT
SIMPLE_LOOPFILTER mmxext, v, 4, 0
SIMPLE_LOOPFILTER mmxext, h, 5, 0
INIT_XMM
%define SPLATB_REG SPLATB_REG_SSE2
%define WRITE_8W   WRITE_8W_SSE2
SIMPLE_LOOPFILTER sse2,   v, 3, 8
SIMPLE_LOOPFILTER sse2,   h, 5, 8
%define SPLATB_REG SPLATB_REG_SSSE3
SIMPLE_LOOPFILTER ssse3,  v, 3, 8
SIMPLE_LOOPFILTER ssse3,  h, 5, 8
%define WRITE_8W   WRITE_8W_SSE4
SIMPLE_LOOPFILTER sse4,   h, 5, 8

;-----------------------------------------------------------------------------
; void vp8_h/v_loop_filter<size>_inner_<opt>(uint8_t *dst, [uint8_t *v,] int stride,
;                                            int flimE, int flimI, int hev_thr);
;-----------------------------------------------------------------------------

%macro INNER_LOOPFILTER 5
%if %4 == 8 ; chroma
cglobal vp8_%2_loop_filter8uv_inner_%1, 6, %3, %5
%define dst8_reg    r1
%define mstride_reg r2
%define E_reg       r3
%define I_reg       r4
%define hev_thr_reg r5
%else ; luma
cglobal vp8_%2_loop_filter16y_inner_%1, 5, %3, %5
%define mstride_reg r1
%define E_reg       r2
%define I_reg       r3
%define hev_thr_reg r4
%ifdef m8 ; x86-64, sse2
%define dst8_reg    r4
%elif mmsize == 16 ; x86-32, sse2
%define dst8_reg    r5
%else ; x86-32, mmx/mmxext
%define cnt_reg     r5
%endif
%endif
%define dst_reg     r0
%define stride_reg  E_reg
%define dst2_reg    I_reg
%ifndef m8
%define stack_reg   hev_thr_reg
%endif

%ifnidn %1, sse2
%if mmsize == 16
    pxor             m7, m7
%endif
%endif

%ifndef m8 ; mmx/mmxext or sse2 on x86-32
    ; splat function arguments
    SPLATB_REG       m0, E_reg, m7   ; E
    SPLATB_REG       m1, I_reg, m7   ; I
    SPLATB_REG       m2, hev_thr_reg, m7 ; hev_thresh

    ; align stack
    mov       stack_reg, rsp         ; backup stack pointer
    and             rsp, ~(mmsize-1) ; align stack
%ifidn %2, v
    sub             rsp, mmsize * 4  ; stack layout: [0]=E, [1]=I, [2]=hev_thr
                                     ;               [3]=hev() result
%else ; h
    sub             rsp, mmsize * 5  ; extra storage space for transposes
%endif

%define flim_E   [rsp]
%define flim_I   [rsp+mmsize]
%define hev_thr  [rsp+mmsize*2]
%define mask_res [rsp+mmsize*3]
%define p0backup [rsp+mmsize*3]
%define q0backup [rsp+mmsize*4]

    mova         flim_E, m0
    mova         flim_I, m1
    mova        hev_thr, m2

%else ; sse2 on x86-64

%define flim_E   m9
%define flim_I   m10
%define hev_thr  m11
%define mask_res m12
%define p0backup m12
%define q0backup m8

    ; splat function arguments
    SPLATB_REG   flim_E, E_reg, m7   ; E
    SPLATB_REG   flim_I, I_reg, m7   ; I
    SPLATB_REG  hev_thr, hev_thr_reg, m7 ; hev_thresh
%endif

%if mmsize == 8 && %4 == 16 ; mmx/mmxext
    mov         cnt_reg, 2
%endif
    mov      stride_reg, mstride_reg
    neg     mstride_reg
%ifidn %2, h
    lea         dst_reg, [dst_reg + stride_reg*4-4]
%if %4 == 8
    lea        dst8_reg, [dst8_reg+ stride_reg*4-4]
%endif
%endif

%if mmsize == 8
.next8px
%endif
    ; read
    lea        dst2_reg, [dst_reg + stride_reg]
%ifidn %2, v
%if %4 == 8 && mmsize == 16
%define movrow movh
%else
%define movrow mova
%endif
    movrow           m0, [dst_reg +mstride_reg*4] ; p3
    movrow           m1, [dst2_reg+mstride_reg*4] ; p2
    movrow           m2, [dst_reg +mstride_reg*2] ; p1
    movrow           m5, [dst2_reg]               ; q1
    movrow           m6, [dst2_reg+ stride_reg]   ; q2
    movrow           m7, [dst2_reg+ stride_reg*2] ; q3
%if mmsize == 16 && %4 == 8
    movhps           m0, [dst8_reg+mstride_reg*4]
    movhps           m2, [dst8_reg+mstride_reg*2]
    add        dst8_reg, stride_reg
    movhps           m1, [dst8_reg+mstride_reg*4]
    movhps           m5, [dst8_reg]
    movhps           m6, [dst8_reg+ stride_reg]
    movhps           m7, [dst8_reg+ stride_reg*2]
    add        dst8_reg, mstride_reg
%endif
%elif mmsize == 8 ; mmx/mmxext (h)
    ; read 8 rows of 8px each
    movu             m0, [dst_reg +mstride_reg*4]
    movu             m1, [dst2_reg+mstride_reg*4]
    movu             m2, [dst_reg +mstride_reg*2]
    movu             m3, [dst_reg +mstride_reg]
    movu             m4, [dst_reg]
    movu             m5, [dst2_reg]
    movu             m6, [dst2_reg+ stride_reg]

    ; 8x8 transpose
    TRANSPOSE4x4B     0, 1, 2, 3, 7
    mova       q0backup, m1
    movu             m7, [dst2_reg+ stride_reg*2]
    TRANSPOSE4x4B     4, 5, 6, 7, 1
    SBUTTERFLY       dq, 0, 4, 1     ; p3/p2
    SBUTTERFLY       dq, 2, 6, 1     ; q0/q1
    SBUTTERFLY       dq, 3, 7, 1     ; q2/q3
    mova             m1, q0backup
    mova       q0backup, m2          ; store q0
    SBUTTERFLY       dq, 1, 5, 2     ; p1/p0
    mova       p0backup, m5          ; store p0
    SWAP              1, 4
    SWAP              2, 4
    SWAP              6, 3
    SWAP              5, 3
%else ; sse2 (h)
%if %4 == 16
    lea        dst8_reg, [dst_reg + stride_reg*8]
%endif

    ; read 16 rows of 8px each, interleave
    movh             m0, [dst_reg +mstride_reg*4]
    movh             m1, [dst8_reg+mstride_reg*4]
    movh             m2, [dst_reg +mstride_reg*2]
    movh             m5, [dst8_reg+mstride_reg*2]
    movh             m3, [dst_reg +mstride_reg]
    movh             m6, [dst8_reg+mstride_reg]
    movh             m4, [dst_reg]
    movh             m7, [dst8_reg]
    punpcklbw        m0, m1          ; A/I
    punpcklbw        m2, m5          ; C/K
    punpcklbw        m3, m6          ; D/L
    punpcklbw        m4, m7          ; E/M

    add        dst8_reg, stride_reg
    movh             m1, [dst2_reg+mstride_reg*4]
    movh             m6, [dst8_reg+mstride_reg*4]
    movh             m5, [dst2_reg]
    movh             m7, [dst8_reg]
    punpcklbw        m1, m6          ; B/J
    punpcklbw        m5, m7          ; F/N
    movh             m6, [dst2_reg+ stride_reg]
    movh             m7, [dst8_reg+ stride_reg]
    punpcklbw        m6, m7          ; G/O

    ; 8x16 transpose
    TRANSPOSE4x4B     0, 1, 2, 3, 7
%ifdef m8
    SWAP              1, 8
%else
    mova       q0backup, m1
%endif
    movh             m7, [dst2_reg+ stride_reg*2]
    movh             m1, [dst8_reg+ stride_reg*2]
    punpcklbw        m7, m1          ; H/P
    TRANSPOSE4x4B     4, 5, 6, 7, 1
    SBUTTERFLY       dq, 0, 4, 1     ; p3/p2
    SBUTTERFLY       dq, 2, 6, 1     ; q0/q1
    SBUTTERFLY       dq, 3, 7, 1     ; q2/q3
%ifdef m8
    SWAP              1, 8
    SWAP              2, 8
%else
    mova             m1, q0backup
    mova       q0backup, m2          ; store q0
%endif
    SBUTTERFLY       dq, 1, 5, 2     ; p1/p0
%ifdef m12
    SWAP              5, 12
%else
    mova       p0backup, m5          ; store p0
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

%ifidn %1, mmx
    mova             m4, flim_I
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
%ifidn %2, v
    movrow           m3, [dst_reg +mstride_reg] ; p0
%if mmsize == 16 && %4 == 8
    movhps           m3, [dst8_reg+mstride_reg]
%endif
%elifdef m12
    SWAP              3, 12
%else
    mova             m3, p0backup
%endif

    mova             m1, m2
    SWAP              1, 2
    mova             m6, m3
    SWAP              3, 6
    psubusb          m1, m3          ; p1-p0
    psubusb          m6, m2          ; p0-p1
    por              m1, m6          ; abs(p1-p0)
%ifidn %1, mmx
    mova             m6, m1
    psubusb          m1, m4
    psubusb          m6, hev_thr
    pcmpeqb          m1, m7          ; abs(p1-p0) <= I
    pcmpeqb          m6, m7          ; abs(p1-p0) <= hev_thresh
    pand             m0, m1
    mova       mask_res, m6
%else ; mmxext/sse2
    pmaxub           m0, m1          ; max_I
    SWAP              1, 4           ; max_hev_thresh
%endif

    SWAP              6, 4           ; now m6 is I
%ifidn %2, v
    movrow           m4, [dst_reg]   ; q0
%if mmsize == 16 && %4 == 8
    movhps           m4, [dst8_reg]
%endif
%elifdef m8
    SWAP              4, 8
%else
    mova             m4, q0backup
%endif
    mova             m1, m4
    SWAP              1, 4
    mova             m7, m5
    SWAP              7, 5
    psubusb          m1, m5          ; q0-q1
    psubusb          m7, m4          ; q1-q0
    por              m1, m7          ; abs(q1-q0)
%ifidn %1, mmx
    mova             m7, m1
    psubusb          m1, m6
    psubusb          m7, hev_thr
    pxor             m6, m6
    pcmpeqb          m1, m6          ; abs(q1-q0) <= I
    pcmpeqb          m7, m6          ; abs(q1-q0) <= hev_thresh
    mova             m6, mask_res
    pand             m0, m1          ; abs([pq][321]-[pq][210]) <= I
    pand             m6, m7
%else ; mmxext/sse2
    pxor             m7, m7
    pmaxub           m0, m1
    pmaxub           m6, m1
    psubusb          m0, flim_I
    psubusb          m6, hev_thr
    pcmpeqb          m0, m7          ; max(abs(..)) <= I
    pcmpeqb          m6, m7          ; !(max(abs..) > thresh)
%endif
%ifdef m12
    SWAP              6, 12
%else
    mova       mask_res, m6          ; !(abs(p1-p0) > hev_t || abs(q1-q0) > hev_t)
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
    psubusb          m7, flim_E
    pcmpeqb          m7, m6          ; abs(q0-p0)*2+abs(q1-p1)/2 <= E
    pand             m0, m7          ; normal_limit result

    ; filter_common; at this point, m2-m5=p1-q1 and m0 is filter_mask
%ifdef m8 ; x86-64 && sse2
    mova             m8, [pb_80]
%define pb_80_var m8
%else ; x86-32 or mmx/mmxext
%define pb_80_var [pb_80]
%endif
    mova             m1, m4
    mova             m7, m3
    pxor             m1, pb_80_var
    pxor             m7, pb_80_var
    psubsb           m1, m7          ; (signed) q0-p0
    mova             m6, m2
    mova             m7, m5
    pxor             m6, pb_80_var
    pxor             m7, pb_80_var
    psubsb           m6, m7          ; (signed) p1-q1
    mova             m7, mask_res
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
    mova             m6, mask_res
%endif
%ifidn %1, mmx
    mova             m7, [pb_1]
%else ; mmxext/sse2
    pxor             m7, m7
%endif
    pand             m0, m6
    pand             m1, m6
%ifidn %1, mmx
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
%ifidn %2, v
    movrow [dst_reg +mstride_reg*2], m2
    movrow [dst_reg +mstride_reg  ], m3
    movrow    [dst_reg], m4
    movrow [dst_reg + stride_reg  ], m5
%if mmsize == 16 && %4 == 8
    movhps [dst8_reg+mstride_reg*2], m2
    movhps [dst8_reg+mstride_reg  ], m3
    movhps   [dst8_reg], m4
    movhps [dst8_reg+ stride_reg  ], m5
%endif
%else ; h
    add         dst_reg, 2
    add        dst2_reg, 2

    ; 4x8/16 transpose
    TRANSPOSE4x4B     2, 3, 4, 5, 6

%if mmsize == 8 ; mmx/mmxext (h)
    WRITE_4x2D        2, 3, 4, 5, dst_reg, dst2_reg, mstride_reg, stride_reg
%else ; sse2 (h)
    lea        dst8_reg, [dst8_reg+mstride_reg+2]
    WRITE_4x4D        2, 3, 4, 5, dst_reg, dst2_reg, dst8_reg, mstride_reg, stride_reg, %4
%endif
%endif

%if mmsize == 8
%if %4 == 8 ; chroma
%ifidn %2, h
    sub         dst_reg, 2
%endif
    cmp         dst_reg, dst8_reg
    mov         dst_reg, dst8_reg
    jnz .next8px
%else
%ifidn %2, h
    lea         dst_reg, [dst_reg + stride_reg*8-2]
%else ; v
    add         dst_reg, 8
%endif
    dec         cnt_reg
    jg .next8px
%endif
%endif

%ifndef m8 ; sse2 on x86-32 or mmx/mmxext
    mov             rsp, stack_reg   ; restore stack pointer
%endif
    RET
%endmacro

INIT_MMX
%define SPLATB_REG SPLATB_REG_MMX
INNER_LOOPFILTER mmx,    v, 6, 16, 0
INNER_LOOPFILTER mmx,    h, 6, 16, 0
INNER_LOOPFILTER mmx,    v, 6,  8, 0
INNER_LOOPFILTER mmx,    h, 6,  8, 0

%define SPLATB_REG SPLATB_REG_MMXEXT
INNER_LOOPFILTER mmxext, v, 6, 16, 0
INNER_LOOPFILTER mmxext, h, 6, 16, 0
INNER_LOOPFILTER mmxext, v, 6,  8, 0
INNER_LOOPFILTER mmxext, h, 6,  8, 0

INIT_XMM
%define SPLATB_REG SPLATB_REG_SSE2
INNER_LOOPFILTER sse2,   v, 5, 16, 13
%ifdef m8
INNER_LOOPFILTER sse2,   h, 5, 16, 13
%else
INNER_LOOPFILTER sse2,   h, 6, 16, 13
%endif
INNER_LOOPFILTER sse2,   v, 6,  8, 13
INNER_LOOPFILTER sse2,   h, 6,  8, 13

%define SPLATB_REG SPLATB_REG_SSSE3
INNER_LOOPFILTER ssse3,  v, 5, 16, 13
%ifdef m8
INNER_LOOPFILTER ssse3,  h, 5, 16, 13
%else
INNER_LOOPFILTER ssse3,  h, 6, 16, 13
%endif
INNER_LOOPFILTER ssse3,  v, 6,  8, 13
INNER_LOOPFILTER ssse3,  h, 6,  8, 13

;-----------------------------------------------------------------------------
; void vp8_h/v_loop_filter<size>_mbedge_<opt>(uint8_t *dst, [uint8_t *v,] int stride,
;                                            int flimE, int flimI, int hev_thr);
;-----------------------------------------------------------------------------

%macro MBEDGE_LOOPFILTER 5
%if %4 == 8 ; chroma
cglobal vp8_%2_loop_filter8uv_mbedge_%1, 6, %3, %5
%define dst8_reg    r1
%define mstride_reg r2
%define E_reg       r3
%define I_reg       r4
%define hev_thr_reg r5
%else ; luma
cglobal vp8_%2_loop_filter16y_mbedge_%1, 5, %3, %5
%define mstride_reg r1
%define E_reg       r2
%define I_reg       r3
%define hev_thr_reg r4
%ifdef m8 ; x86-64, sse2
%define dst8_reg    r4
%elif mmsize == 16 ; x86-32, sse2
%define dst8_reg    r5
%else ; x86-32, mmx/mmxext
%define cnt_reg     r5
%endif
%endif
%define dst_reg     r0
%define stride_reg  E_reg
%define dst2_reg    I_reg
%ifndef m8
%define stack_reg   hev_thr_reg
%endif

%define ssse3_or_higher 0
%ifnidn %1, sse2
%if mmsize == 16
%define ssse3_or_higher 1
%endif
%endif

%if ssse3_or_higher
    pxor             m7, m7
%endif

%ifndef m8 ; mmx/mmxext or sse2 on x86-32
    ; splat function arguments
    SPLATB_REG       m0, E_reg, m7   ; E
    SPLATB_REG       m1, I_reg, m7   ; I
    SPLATB_REG       m2, hev_thr_reg, m7 ; hev_thresh

    ; align stack
    mov       stack_reg, rsp         ; backup stack pointer
    and             rsp, ~(mmsize-1) ; align stack
%if mmsize == 16
    sub             rsp, mmsize * 7
%else
    sub             rsp, mmsize * 8  ; stack layout: [0]=E, [1]=I, [2]=hev_thr
                                     ;               [3]=hev() result
                                     ;               [4]=filter tmp result
                                     ;               [5]/[6] = p2/q2 backup
                                     ;               [7]=lim_res sign result
%endif

%define flim_E   [rsp]
%define flim_I   [rsp+mmsize]
%define hev_thr  [rsp+mmsize*2]
%define mask_res [rsp+mmsize*3]
%define lim_res  [rsp+mmsize*4]
%define p0backup [rsp+mmsize*3]
%define q0backup [rsp+mmsize*4]
%define p2backup [rsp+mmsize*5]
%define q2backup [rsp+mmsize*6]
%if mmsize == 16
%define lim_sign [rsp]
%else
%define lim_sign [rsp+mmsize*7]
%endif

    mova         flim_E, m0
    mova         flim_I, m1
    mova        hev_thr, m2

%else ; sse2 on x86-64

%define flim_E   m9
%define flim_I   m10
%define hev_thr  m11
%define mask_res m12
%define lim_res  m8
%define p0backup m12
%define q0backup m8
%define p2backup m13
%define q2backup m14
%define lim_sign m9

    ; splat function arguments
    SPLATB_REG   flim_E, E_reg, m7   ; E
    SPLATB_REG   flim_I, I_reg, m7   ; I
    SPLATB_REG  hev_thr, hev_thr_reg, m7 ; hev_thresh
%endif

%if mmsize == 8 && %4 == 16 ; mmx/mmxext
    mov         cnt_reg, 2
%endif
    mov      stride_reg, mstride_reg
    neg     mstride_reg
%ifidn %2, h
    lea         dst_reg, [dst_reg + stride_reg*4-4]
%if %4 == 8
    lea        dst8_reg, [dst8_reg+ stride_reg*4-4]
%endif
%endif

%if mmsize == 8
.next8px
%endif
    ; read
    lea        dst2_reg, [dst_reg + stride_reg]
%ifidn %2, v
%if %4 == 8 && mmsize == 16
%define movrow movh
%else
%define movrow mova
%endif
    movrow           m0, [dst_reg +mstride_reg*4] ; p3
    movrow           m1, [dst2_reg+mstride_reg*4] ; p2
    movrow           m2, [dst_reg +mstride_reg*2] ; p1
    movrow           m5, [dst2_reg]               ; q1
    movrow           m6, [dst2_reg+ stride_reg]   ; q2
    movrow           m7, [dst2_reg+ stride_reg*2] ; q3
%if mmsize == 16 && %4 == 8
    movhps           m0, [dst8_reg+mstride_reg*4]
    movhps           m2, [dst8_reg+mstride_reg*2]
    add        dst8_reg, stride_reg
    movhps           m1, [dst8_reg+mstride_reg*4]
    movhps           m5, [dst8_reg]
    movhps           m6, [dst8_reg+ stride_reg]
    movhps           m7, [dst8_reg+ stride_reg*2]
    add        dst8_reg, mstride_reg
%endif
%elif mmsize == 8 ; mmx/mmxext (h)
    ; read 8 rows of 8px each
    movu             m0, [dst_reg +mstride_reg*4]
    movu             m1, [dst2_reg+mstride_reg*4]
    movu             m2, [dst_reg +mstride_reg*2]
    movu             m3, [dst_reg +mstride_reg]
    movu             m4, [dst_reg]
    movu             m5, [dst2_reg]
    movu             m6, [dst2_reg+ stride_reg]

    ; 8x8 transpose
    TRANSPOSE4x4B     0, 1, 2, 3, 7
    mova       q0backup, m1
    movu             m7, [dst2_reg+ stride_reg*2]
    TRANSPOSE4x4B     4, 5, 6, 7, 1
    SBUTTERFLY       dq, 0, 4, 1     ; p3/p2
    SBUTTERFLY       dq, 2, 6, 1     ; q0/q1
    SBUTTERFLY       dq, 3, 7, 1     ; q2/q3
    mova             m1, q0backup
    mova       q0backup, m2          ; store q0
    SBUTTERFLY       dq, 1, 5, 2     ; p1/p0
    mova       p0backup, m5          ; store p0
    SWAP              1, 4
    SWAP              2, 4
    SWAP              6, 3
    SWAP              5, 3
%else ; sse2 (h)
%if %4 == 16
    lea        dst8_reg, [dst_reg + stride_reg*8]
%endif

    ; read 16 rows of 8px each, interleave
    movh             m0, [dst_reg +mstride_reg*4]
    movh             m1, [dst8_reg+mstride_reg*4]
    movh             m2, [dst_reg +mstride_reg*2]
    movh             m5, [dst8_reg+mstride_reg*2]
    movh             m3, [dst_reg +mstride_reg]
    movh             m6, [dst8_reg+mstride_reg]
    movh             m4, [dst_reg]
    movh             m7, [dst8_reg]
    punpcklbw        m0, m1          ; A/I
    punpcklbw        m2, m5          ; C/K
    punpcklbw        m3, m6          ; D/L
    punpcklbw        m4, m7          ; E/M

    add        dst8_reg, stride_reg
    movh             m1, [dst2_reg+mstride_reg*4]
    movh             m6, [dst8_reg+mstride_reg*4]
    movh             m5, [dst2_reg]
    movh             m7, [dst8_reg]
    punpcklbw        m1, m6          ; B/J
    punpcklbw        m5, m7          ; F/N
    movh             m6, [dst2_reg+ stride_reg]
    movh             m7, [dst8_reg+ stride_reg]
    punpcklbw        m6, m7          ; G/O

    ; 8x16 transpose
    TRANSPOSE4x4B     0, 1, 2, 3, 7
%ifdef m8
    SWAP              1, 8
%else
    mova       q0backup, m1
%endif
    movh             m7, [dst2_reg+ stride_reg*2]
    movh             m1, [dst8_reg+ stride_reg*2]
    punpcklbw        m7, m1          ; H/P
    TRANSPOSE4x4B     4, 5, 6, 7, 1
    SBUTTERFLY       dq, 0, 4, 1     ; p3/p2
    SBUTTERFLY       dq, 2, 6, 1     ; q0/q1
    SBUTTERFLY       dq, 3, 7, 1     ; q2/q3
%ifdef m8
    SWAP              1, 8
    SWAP              2, 8
%else
    mova             m1, q0backup
    mova       q0backup, m2          ; store q0
%endif
    SBUTTERFLY       dq, 1, 5, 2     ; p1/p0
%ifdef m12
    SWAP              5, 12
%else
    mova       p0backup, m5          ; store p0
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
    mova       p2backup, m1
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
    mova       q2backup, m6
    psubusb          m6, m5          ; q2-q1
    por              m6, m4          ; abs(q2-q1)

%ifidn %1, mmx
    mova             m4, flim_I
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
%ifidn %2, v
    movrow           m3, [dst_reg +mstride_reg] ; p0
%if mmsize == 16 && %4 == 8
    movhps           m3, [dst8_reg+mstride_reg]
%endif
%elifdef m12
    SWAP              3, 12
%else
    mova             m3, p0backup
%endif

    mova             m1, m2
    SWAP              1, 2
    mova             m6, m3
    SWAP              3, 6
    psubusb          m1, m3          ; p1-p0
    psubusb          m6, m2          ; p0-p1
    por              m1, m6          ; abs(p1-p0)
%ifidn %1, mmx
    mova             m6, m1
    psubusb          m1, m4
    psubusb          m6, hev_thr
    pcmpeqb          m1, m7          ; abs(p1-p0) <= I
    pcmpeqb          m6, m7          ; abs(p1-p0) <= hev_thresh
    pand             m0, m1
    mova       mask_res, m6
%else ; mmxext/sse2
    pmaxub           m0, m1          ; max_I
    SWAP              1, 4           ; max_hev_thresh
%endif

    SWAP              6, 4           ; now m6 is I
%ifidn %2, v
    movrow           m4, [dst_reg]   ; q0
%if mmsize == 16 && %4 == 8
    movhps           m4, [dst8_reg]
%endif
%elifdef m8
    SWAP              4, 8
%else
    mova             m4, q0backup
%endif
    mova             m1, m4
    SWAP              1, 4
    mova             m7, m5
    SWAP              7, 5
    psubusb          m1, m5          ; q0-q1
    psubusb          m7, m4          ; q1-q0
    por              m1, m7          ; abs(q1-q0)
%ifidn %1, mmx
    mova             m7, m1
    psubusb          m1, m6
    psubusb          m7, hev_thr
    pxor             m6, m6
    pcmpeqb          m1, m6          ; abs(q1-q0) <= I
    pcmpeqb          m7, m6          ; abs(q1-q0) <= hev_thresh
    mova             m6, mask_res
    pand             m0, m1          ; abs([pq][321]-[pq][210]) <= I
    pand             m6, m7
%else ; mmxext/sse2
    pxor             m7, m7
    pmaxub           m0, m1
    pmaxub           m6, m1
    psubusb          m0, flim_I
    psubusb          m6, hev_thr
    pcmpeqb          m0, m7          ; max(abs(..)) <= I
    pcmpeqb          m6, m7          ; !(max(abs..) > thresh)
%endif
%ifdef m12
    SWAP              6, 12
%else
    mova       mask_res, m6          ; !(abs(p1-p0) > hev_t || abs(q1-q0) > hev_t)
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
    psubusb          m7, flim_E
    pcmpeqb          m7, m6          ; abs(q0-p0)*2+abs(q1-p1)/2 <= E
    pand             m0, m7          ; normal_limit result

    ; filter_common; at this point, m2-m5=p1-q1 and m0 is filter_mask
%ifdef m8 ; x86-64 && sse2
    mova             m8, [pb_80]
%define pb_80_var m8
%else ; x86-32 or mmx/mmxext
%define pb_80_var [pb_80]
%endif
    mova             m1, m4
    mova             m7, m3
    pxor             m1, pb_80_var
    pxor             m7, pb_80_var
    psubsb           m1, m7          ; (signed) q0-p0
    mova             m6, m2
    mova             m7, m5
    pxor             m6, pb_80_var
    pxor             m7, pb_80_var
    psubsb           m6, m7          ; (signed) p1-q1
    mova             m7, mask_res
    paddsb           m6, m1
    paddsb           m6, m1
    paddsb           m6, m1
    pand             m6, m0
%ifdef m8
    mova        lim_res, m6          ; 3*(qp-p0)+(p1-q1) masked for filter_mbedge
    pand        lim_res, m7
%else
    mova             m0, m6
    pand             m0, m7
    mova        lim_res, m0
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
%if ssse3_or_higher
    mova             m7, [pb_1]
%else
    mova             m7, [pw_63]
%endif
%ifdef m8
    SWAP              1, 8
%else
    mova             m1, lim_res
%endif
    pxor             m0, m0
    mova             m6, m1
    pcmpgtb          m0, m1         ; which are negative
%if ssse3_or_higher
    punpcklbw        m6, m7         ; interleave with "1" for rounding
    punpckhbw        m1, m7
%else
    punpcklbw        m6, m0         ; signed byte->word
    punpckhbw        m1, m0
%endif
    mova       lim_sign, m0
%if ssse3_or_higher
    mova             m7, [pb_27_63]
%ifndef m8
    mova        lim_res, m1
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
    mova             m0, lim_sign
%endif
%else
    mova       mask_res, m6         ; backup for later in filter
    mova        lim_res, m1
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
%if ssse3_or_higher
    mova            m6, [pb_18_63]  ; pipelining
%endif
    psubusb         m3, m1
    paddusb         m4, m1
    paddusb         m3, m0          ; p0+a0
    psubusb         m4, m0          ; q0-a0

%if ssse3_or_higher
    SWAP             6, 7
%ifdef m10
    SWAP             1, 10
%else
    mova            m1, lim_res
%endif
    mova            m0, m7
    pmaddubsw       m7, m6
    SWAP             6, 7
    pmaddubsw       m0, m1
    SWAP             1, 0
%ifdef m10
    SWAP             0, 10
%endif
    mova            m0, lim_sign
%else
    mova            m6, mask_res
    mova            m1, lim_res
    pmullw          m6, [pw_18]
    pmullw          m1, [pw_18]
    paddw           m6, m7
    paddw           m1, m7
%endif
    mova            m0, lim_sign
    psraw           m6, 7
    psraw           m1, 7
    packsswb        m6, m1          ; a1
    pxor            m1, m1
    psubb           m1, m6
    pand            m1, m0          ; -a1
    pandn           m0, m6          ; +a1
%if ssse3_or_higher
    mova            m6, [pb_9_63]
%endif
    psubusb         m2, m1
    paddusb         m5, m1
    paddusb         m2, m0          ; p1+a1
    psubusb         m5, m0          ; q1-a1

%if ssse3_or_higher
    SWAP             6, 7
%ifdef m10
    SWAP             1, 10
%else
    mova            m1, lim_res
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
    mova            m6, mask_res
    mova            m1, lim_res
%endif
    pmullw          m6, [pw_9]
    pmullw          m1, [pw_9]
    paddw           m6, m7
    paddw           m1, m7
%endif
%ifdef m9
    SWAP             7, 9
%else
    mova            m7, lim_sign
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
    mova            m1, p2backup
    mova            m6, q2backup
%endif
    psubusb         m1, m0
    paddusb         m6, m0
    paddusb         m1, m7          ; p1+a1
    psubusb         m6, m7          ; q1-a1

    ; store
%ifidn %2, v
    movrow [dst2_reg+mstride_reg*4], m1
    movrow [dst_reg +mstride_reg*2], m2
    movrow [dst_reg +mstride_reg  ], m3
    movrow    [dst_reg], m4
    movrow   [dst2_reg], m5
    movrow [dst2_reg+ stride_reg  ], m6
%if mmsize == 16 && %4 == 8
    add        dst8_reg, mstride_reg
    movhps [dst8_reg+mstride_reg*2], m1
    movhps [dst8_reg+mstride_reg  ], m2
    movhps   [dst8_reg], m3
    add        dst8_reg, stride_reg
    movhps   [dst8_reg], m4
    movhps [dst8_reg+ stride_reg  ], m5
    movhps [dst8_reg+ stride_reg*2], m6
%endif
%else ; h
    inc         dst_reg
    inc        dst2_reg

    ; 4x8/16 transpose
    TRANSPOSE4x4B     1, 2, 3, 4, 0
    SBUTTERFLY       bw, 5, 6, 0

%if mmsize == 8 ; mmx/mmxext (h)
    WRITE_4x2D        1, 2, 3, 4, dst_reg, dst2_reg, mstride_reg, stride_reg
    add         dst_reg, 4
    WRITE_2x4W       m5, m6, dst2_reg, dst_reg, mstride_reg, stride_reg
%else ; sse2 (h)
    lea        dst8_reg, [dst8_reg+mstride_reg+1]
    WRITE_4x4D        1, 2, 3, 4, dst_reg, dst2_reg, dst8_reg, mstride_reg, stride_reg, %4
    lea         dst_reg, [dst2_reg+mstride_reg+4]
    lea        dst8_reg, [dst8_reg+mstride_reg+4]
%ifidn %1, sse4
    add        dst2_reg, 4
%endif
    WRITE_8W         m5, dst2_reg, dst_reg,  mstride_reg, stride_reg
%ifidn %1, sse4
    lea        dst2_reg, [dst8_reg+ stride_reg]
%endif
    WRITE_8W         m6, dst2_reg, dst8_reg, mstride_reg, stride_reg
%endif
%endif

%if mmsize == 8
%if %4 == 8 ; chroma
%ifidn %2, h
    sub         dst_reg, 5
%endif
    cmp         dst_reg, dst8_reg
    mov         dst_reg, dst8_reg
    jnz .next8px
%else
%ifidn %2, h
    lea         dst_reg, [dst_reg + stride_reg*8-5]
%else ; v
    add         dst_reg, 8
%endif
    dec         cnt_reg
    jg .next8px
%endif
%endif

%ifndef m8 ; sse2 on x86-32 or mmx/mmxext
    mov             rsp, stack_reg   ; restore stack pointer
%endif
    RET
%endmacro

INIT_MMX
%define SPLATB_REG SPLATB_REG_MMX
MBEDGE_LOOPFILTER mmx,    v, 6, 16, 0
MBEDGE_LOOPFILTER mmx,    h, 6, 16, 0
MBEDGE_LOOPFILTER mmx,    v, 6,  8, 0
MBEDGE_LOOPFILTER mmx,    h, 6,  8, 0

%define SPLATB_REG SPLATB_REG_MMXEXT
MBEDGE_LOOPFILTER mmxext, v, 6, 16, 0
MBEDGE_LOOPFILTER mmxext, h, 6, 16, 0
MBEDGE_LOOPFILTER mmxext, v, 6,  8, 0
MBEDGE_LOOPFILTER mmxext, h, 6,  8, 0

INIT_XMM
%define SPLATB_REG SPLATB_REG_SSE2
%define WRITE_8W   WRITE_8W_SSE2
MBEDGE_LOOPFILTER sse2,   v, 5, 16, 15
%ifdef m8
MBEDGE_LOOPFILTER sse2,   h, 5, 16, 15
%else
MBEDGE_LOOPFILTER sse2,   h, 6, 16, 15
%endif
MBEDGE_LOOPFILTER sse2,   v, 6,  8, 15
MBEDGE_LOOPFILTER sse2,   h, 6,  8, 15

%define SPLATB_REG SPLATB_REG_SSSE3
MBEDGE_LOOPFILTER ssse3,  v, 5, 16, 15
%ifdef m8
MBEDGE_LOOPFILTER ssse3,  h, 5, 16, 15
%else
MBEDGE_LOOPFILTER ssse3,  h, 6, 16, 15
%endif
MBEDGE_LOOPFILTER ssse3,  v, 6,  8, 15
MBEDGE_LOOPFILTER ssse3,  h, 6,  8, 15

%define WRITE_8W   WRITE_8W_SSE4
%ifdef m8
MBEDGE_LOOPFILTER sse4,   h, 5, 16, 15
%else
MBEDGE_LOOPFILTER sse4,   h, 6, 16, 15
%endif
MBEDGE_LOOPFILTER sse4,   h, 6,  8, 15
