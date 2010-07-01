;******************************************************************************
;* H.264 intra prediction asm optimizations
;* Copyright (c) 2010 Jason Garrett-Glaser
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

SECTION_RODATA

tm_shuf: times 8 db 0x03, 0x80

SECTION .text

cextern pb_1
cextern pb_3

;-----------------------------------------------------------------------------
; void pred16x16_vertical(uint8_t *src, int stride)
;-----------------------------------------------------------------------------

cglobal pred16x16_vertical_mmx, 2,3
    sub   r0, r1
    mov   r2, 8
    movq mm0, [r0+0]
    movq mm1, [r0+8]
.loop:
    movq [r0+r1*1+0], mm0
    movq [r0+r1*1+8], mm1
    movq [r0+r1*2+0], mm0
    movq [r0+r1*2+8], mm1
    lea   r0, [r0+r1*2]
    dec   r2
    jg .loop
    REP_RET

cglobal pred16x16_vertical_sse, 2,3
    sub   r0, r1
    mov   r2, 4
    movaps xmm0, [r0]
.loop:
    movaps [r0+r1*1], xmm0
    movaps [r0+r1*2], xmm0
    lea   r0, [r0+r1*2]
    movaps [r0+r1*1], xmm0
    movaps [r0+r1*2], xmm0
    lea   r0, [r0+r1*2]
    dec   r2
    jg .loop
    REP_RET

;-----------------------------------------------------------------------------
; void pred16x16_horizontal(uint8_t *src, int stride)
;-----------------------------------------------------------------------------

%macro PRED16x16_H 1
cglobal pred16x16_horizontal_%1, 2,3
    mov       r2, 8
%ifidn %1, ssse3
    mova      m2, [pb_3]
%endif
.loop:
    movd      m0, [r0+r1*0-4]
    movd      m1, [r0+r1*1-4]

%ifidn %1, ssse3
    pshufb    m0, m2
    pshufb    m1, m2
%else
    punpcklbw m0, m0
    punpcklbw m1, m1
%ifidn %1, mmxext
    pshufw    m0, m0, 0xff
    pshufw    m1, m1, 0xff
%else
    punpckhwd m0, m0
    punpckhwd m1, m1
    punpckhdq m0, m0
    punpckhdq m1, m1
%endif
    mova [r0+r1*0+8], m0
    mova [r0+r1*1+8], m1
%endif

    mova [r0+r1*0], m0
    mova [r0+r1*1], m1
    lea       r0, [r0+r1*2]
    dec       r2
    jg .loop
    REP_RET
%endmacro

INIT_MMX
PRED16x16_H mmx
PRED16x16_H mmxext
INIT_XMM
PRED16x16_H ssse3

;-----------------------------------------------------------------------------
; void pred16x16_dc(uint8_t *src, int stride)
;-----------------------------------------------------------------------------

%macro PRED16x16_DC 1
cglobal pred16x16_dc_%1, 2,7
    mov       r4, r0
    sub       r0, r1
    pxor      mm0, mm0
    pxor      mm1, mm1
    psadbw    mm0, [r0+0]
    psadbw    mm1, [r0+8]
    dec        r0
    movzx     r5d, byte [r0+r1*1]
    paddw     mm0, mm1
    movd      r6d, mm0
    lea        r0, [r0+r1*2]
%rep 7
    movzx     r2d, byte [r0+r1*0]
    movzx     r3d, byte [r0+r1*1]
    add       r5d, r2d
    add       r6d, r3d
    lea        r0, [r0+r1*2]
%endrep
    movzx     r2d, byte [r0+r1*0]
    add       r5d, r6d
    lea       r2d, [r2+r5+16]
    shr       r2d, 5
%ifidn %1, mmxext
    movd       m0, r2d
    punpcklbw  m0, m0
    pshufw     m0, m0, 0
%elifidn %1, sse2
    movd       m0, r2d
    punpcklbw  m0, m0
    pshuflw    m0, m0, 0
    punpcklqdq m0, m0
%elifidn %1, ssse3
    pxor       m1, m1
    movd       m0, r2d
    pshufb     m0, m1
%endif

%if mmsize==8
    mov       r3d, 8
.loop:
    mova [r4+r1*0+0], m0
    mova [r4+r1*0+8], m0
    mova [r4+r1*1+0], m0
    mova [r4+r1*1+8], m0
%else
    mov       r3d, 4
.loop:
    mova [r4+r1*0], m0
    mova [r4+r1*1], m0
    lea   r4, [r4+r1*2]
    mova [r4+r1*0], m0
    mova [r4+r1*1], m0
%endif
    lea   r4, [r4+r1*2]
    dec   r3d
    jg .loop
    REP_RET
%endmacro

INIT_MMX
PRED16x16_DC mmxext
INIT_XMM
PRED16x16_DC   sse2
PRED16x16_DC  ssse3

;-----------------------------------------------------------------------------
; void pred16x16_tm_vp8(uint8_t *src, int stride)
;-----------------------------------------------------------------------------

%macro PRED16x16_TM_MMX 1
cglobal pred16x16_tm_vp8_%1, 2,5
    sub        r0, r1
    pxor      mm7, mm7
    movq      mm0, [r0+0]
    movq      mm2, [r0+8]
    movq      mm1, mm0
    movq      mm3, mm2
    punpcklbw mm0, mm7
    punpckhbw mm1, mm7
    punpcklbw mm2, mm7
    punpckhbw mm3, mm7
    movzx     r3d, byte [r0-1]
    mov       r4d, 16
.loop:
    movzx     r2d, byte [r0+r1-1]
    sub       r2d, r3d
    movd      mm4, r2d
%ifidn %1, mmx
    punpcklwd mm4, mm4
    punpckldq mm4, mm4
%else
    pshufw    mm4, mm4, 0
%endif
    movq      mm5, mm4
    movq      mm6, mm4
    movq      mm7, mm4
    paddw     mm4, mm0
    paddw     mm5, mm1
    paddw     mm6, mm2
    paddw     mm7, mm3
    packuswb  mm4, mm5
    packuswb  mm6, mm7
    movq [r0+r1+0], mm4
    movq [r0+r1+8], mm6
    add        r0, r1
    dec       r4d
    jg .loop
    REP_RET
%endmacro

PRED16x16_TM_MMX mmx
PRED16x16_TM_MMX mmxext

cglobal pred16x16_tm_vp8_sse2, 2,6,6
    sub          r0, r1
    pxor       xmm2, xmm2
    movdqa     xmm0, [r0]
    movdqa     xmm1, xmm0
    punpcklbw  xmm0, xmm2
    punpckhbw  xmm1, xmm2
    movzx       r4d, byte [r0-1]
    mov         r5d, 8
.loop:
    movzx       r2d, byte [r0+r1*1-1]
    movzx       r3d, byte [r0+r1*2-1]
    sub         r2d, r4d
    sub         r3d, r4d
    movd       xmm2, r2d
    movd       xmm4, r3d
    pshuflw    xmm2, xmm2, 0
    pshuflw    xmm4, xmm4, 0
    punpcklqdq xmm2, xmm2
    punpcklqdq xmm4, xmm4
    movdqa     xmm3, xmm2
    movdqa     xmm5, xmm4
    paddw      xmm2, xmm0
    paddw      xmm3, xmm1
    paddw      xmm4, xmm0
    paddw      xmm5, xmm1
    packuswb   xmm2, xmm3
    packuswb   xmm4, xmm5
    movdqa [r0+r1*1], xmm2
    movdqa [r0+r1*2], xmm4
    lea          r0, [r0+r1*2]
    dec         r5d
    jg .loop
    REP_RET

;-----------------------------------------------------------------------------
; void pred8x8_vertical(uint8_t *src, int stride)
;-----------------------------------------------------------------------------

cglobal pred8x8_vertical_mmx, 2,2
    sub    r0, r1
    movq  mm0, [r0]
%rep 3
    movq [r0+r1*1], mm0
    movq [r0+r1*2], mm0
    lea    r0, [r0+r1*2]
%endrep
    movq [r0+r1*1], mm0
    movq [r0+r1*2], mm0
    RET

;-----------------------------------------------------------------------------
; void pred8x8_horizontal(uint8_t *src, int stride)
;-----------------------------------------------------------------------------

%macro PRED8x8_H 1
cglobal pred8x8_horizontal_%1, 2,3
    mov       r2, 4
%ifidn %1, ssse3
    mova      m2, [pb_3]
%endif
.loop:
    movd      m0, [r0+r1*0-4]
    movd      m1, [r0+r1*1-4]
%ifidn %1, ssse3
    pshufb    m0, m2
    pshufb    m1, m2
%else
    punpcklbw m0, m0
    punpcklbw m1, m1
%ifidn %1, mmxext
    pshufw    m0, m0, 0xff
    pshufw    m1, m1, 0xff
%else
    punpckhwd m0, m0
    punpckhwd m1, m1
    punpckhdq m0, m0
    punpckhdq m1, m1
%endif
%endif
    mova [r0+r1*0], m0
    mova [r0+r1*1], m1
    lea       r0, [r0+r1*2]
    dec       r2
    jg .loop
    REP_RET
%endmacro

INIT_MMX
PRED8x8_H mmx
PRED8x8_H mmxext
PRED8x8_H ssse3

;-----------------------------------------------------------------------------
; void pred8x8_dc_rv40(uint8_t *src, int stride)
;-----------------------------------------------------------------------------

cglobal pred8x8_dc_rv40_mmxext, 2,7
    mov       r4, r0
    sub       r0, r1
    pxor      mm0, mm0
    psadbw    mm0, [r0]
    dec        r0
    movzx     r5d, byte [r0+r1*1]
    movd      r6d, mm0
    lea        r0, [r0+r1*2]
%rep 3
    movzx     r2d, byte [r0+r1*0]
    movzx     r3d, byte [r0+r1*1]
    add       r5d, r2d
    add       r6d, r3d
    lea        r0, [r0+r1*2]
%endrep
    movzx     r2d, byte [r0+r1*0]
    add       r5d, r6d
    lea       r2d, [r2+r5+8]
    shr       r2d, 4
    movd      mm0, r2d
    punpcklbw mm0, mm0
    pshufw    mm0, mm0, 0
    mov       r3d, 4
.loop:
    movq [r4+r1*0], mm0
    movq [r4+r1*1], mm0
    lea   r4, [r4+r1*2]
    dec   r3d
    jg .loop
    REP_RET

;-----------------------------------------------------------------------------
; void pred8x8_tm_vp8(uint8_t *src, int stride)
;-----------------------------------------------------------------------------

%macro PRED8x8_TM_MMX 1
cglobal pred8x8_tm_vp8_%1, 2,6
    sub        r0, r1
    pxor      mm7, mm7
    movq      mm0, [r0]
    movq      mm1, mm0
    punpcklbw mm0, mm7
    punpckhbw mm1, mm7
    movzx     r4d, byte [r0-1]
    mov       r5d, 4
.loop:
    movzx     r2d, byte [r0+r1*1-1]
    movzx     r3d, byte [r0+r1*2-1]
    sub       r2d, r4d
    sub       r3d, r4d
    movd      mm2, r2d
    movd      mm4, r3d
%ifidn %1, mmx
    punpcklwd mm2, mm2
    punpcklwd mm4, mm4
    punpckldq mm2, mm2
    punpckldq mm4, mm4
%else
    pshufw    mm2, mm2, 0
    pshufw    mm4, mm4, 0
%endif
    movq      mm3, mm2
    movq      mm5, mm4
    paddw     mm2, mm0
    paddw     mm3, mm1
    paddw     mm4, mm0
    paddw     mm5, mm1
    packuswb  mm2, mm3
    packuswb  mm4, mm5
    movq [r0+r1*1], mm2
    movq [r0+r1*2], mm4
    lea        r0, [r0+r1*2]
    dec       r5d
    jg .loop
    REP_RET
%endmacro

PRED8x8_TM_MMX mmx
PRED8x8_TM_MMX mmxext

cglobal pred8x8_tm_vp8_sse2, 2,6,4
    sub          r0, r1
    pxor       xmm1, xmm1
    movq       xmm0, [r0]
    punpcklbw  xmm0, xmm1
    movzx       r4d, byte [r0-1]
    mov         r5d, 4
.loop:
    movzx       r2d, byte [r0+r1*1-1]
    movzx       r3d, byte [r0+r1*2-1]
    sub         r2d, r4d
    sub         r3d, r4d
    movd       xmm2, r2d
    movd       xmm3, r3d
    pshuflw    xmm2, xmm2, 0
    pshuflw    xmm3, xmm3, 0
    punpcklqdq xmm2, xmm2
    punpcklqdq xmm3, xmm3
    paddw      xmm2, xmm0
    paddw      xmm3, xmm0
    packuswb   xmm2, xmm3
    movq   [r0+r1*1], xmm2
    movhps [r0+r1*2], xmm2
    lea          r0, [r0+r1*2]
    dec         r5d
    jg .loop
    REP_RET

cglobal pred8x8_tm_vp8_ssse3, 2,3,6
    sub          r0, r1
    movdqa     xmm4, [tm_shuf]
    pxor       xmm1, xmm1
    movq       xmm0, [r0]
    punpcklbw  xmm0, xmm1
    movd       xmm5, [r0-4]
    pshufb     xmm5, xmm4
    mov         r2d, 4
.loop:
    movd       xmm2, [r0+r1*1-4]
    movd       xmm3, [r0+r1*2-4]
    pshufb     xmm2, xmm4
    pshufb     xmm3, xmm4
    psubw      xmm2, xmm5
    psubw      xmm3, xmm5
    paddw      xmm2, xmm0
    paddw      xmm3, xmm0
    packuswb   xmm2, xmm3
    movq   [r0+r1*1], xmm2
    movhps [r0+r1*2], xmm2
    lea          r0, [r0+r1*2]
    dec         r2d
    jg .loop
    REP_RET

;-----------------------------------------------------------------------------
; void pred4x4_dc_mmxext(uint8_t *src, const uint8_t *topright, int stride)
;-----------------------------------------------------------------------------

cglobal pred4x4_dc_mmxext, 3,5
    pxor   mm7, mm7
    mov     r4, r0
    sub     r0, r2
    movd   mm0, [r0]
    psadbw mm0, mm7
    movzx  r1d, byte [r0+r2*1-1]
    movd   r3d, mm0
    add    r3d, r1d
    movzx  r1d, byte [r0+r2*2-1]
    lea     r0, [r0+r2*2]
    add    r3d, r1d
    movzx  r1d, byte [r0+r2*1-1]
    add    r3d, r1d
    movzx  r1d, byte [r0+r2*2-1]
    add    r3d, r1d
    add    r3d, 4
    shr    r3d, 3
    imul   r3d, 0x01010101
    mov   [r4+r2*0], r3d
    mov   [r0+r2*0], r3d
    mov   [r0+r2*1], r3d
    mov   [r0+r2*2], r3d
    RET

;-----------------------------------------------------------------------------
; void pred4x4_tm_vp8_mmxext(uint8_t *src, const uint8_t *topright, int stride)
;-----------------------------------------------------------------------------

%macro PRED4x4_TM_MMX 1
cglobal pred4x4_tm_vp8_%1, 3,6
    sub        r0, r2
    pxor      mm7, mm7
    movd      mm0, [r0]
    punpcklbw mm0, mm7
    movzx     r4d, byte [r0-1]
    mov       r5d, 2
.loop:
    movzx     r1d, byte [r0+r2*1-1]
    movzx     r3d, byte [r0+r2*2-1]
    sub       r1d, r4d
    sub       r3d, r4d
    movd      mm2, r1d
    movd      mm4, r3d
%ifidn %1, mmx
    punpcklwd mm2, mm2
    punpcklwd mm4, mm4
    punpckldq mm2, mm2
    punpckldq mm4, mm4
%else
    pshufw    mm2, mm2, 0
    pshufw    mm4, mm4, 0
%endif
    paddw     mm2, mm0
    paddw     mm4, mm0
    packuswb  mm2, mm2
    packuswb  mm4, mm4
    movd [r0+r2*1], mm2
    movd [r0+r2*2], mm4
    lea        r0, [r0+r2*2]
    dec       r5d
    jg .loop
    REP_RET
%endmacro

PRED4x4_TM_MMX mmx
PRED4x4_TM_MMX mmxext

cglobal pred4x4_tm_vp8_ssse3, 3,3
    sub         r0, r2
    movq       mm6, [tm_shuf]
    pxor       mm1, mm1
    movd       mm0, [r0]
    punpcklbw  mm0, mm1
    movd       mm7, [r0-4]
    pshufb     mm7, mm6
    lea         r1, [r0+r2*2]
    movd       mm2, [r0+r2*1-4]
    movd       mm3, [r0+r2*2-4]
    movd       mm4, [r1+r2*1-4]
    movd       mm5, [r1+r2*2-4]
    pshufb     mm2, mm6
    pshufb     mm3, mm6
    pshufb     mm4, mm6
    pshufb     mm5, mm6
    psubw      mm2, mm7
    psubw      mm3, mm7
    psubw      mm4, mm7
    psubw      mm5, mm7
    paddw      mm2, mm0
    paddw      mm3, mm0
    paddw      mm4, mm0
    paddw      mm5, mm0
    packuswb   mm2, mm2
    packuswb   mm3, mm3
    packuswb   mm4, mm4
    packuswb   mm5, mm5
    movd [r0+r2*1], mm2
    movd [r0+r2*2], mm3
    movd [r1+r2*1], mm4
    movd [r1+r2*2], mm5
    RET

; dest, left, right, src, tmp
; output: %1 = (t[n-1] + t[n]*2 + t[n+1] + 2) >> 2
%macro PRED4x4_LOWPASS 5
    mova    %5, %2
    pavgb   %2, %3
    pxor    %3, %5
    mova    %1, %4
    pand    %3, [pb_1]
    psubusb %2, %3
    pavgb   %1, %2
%endmacro

;-----------------------------------------------------------------------------
; void pred4x4_vertical_vp8_mmxext(uint8_t *src, const uint8_t *topright, int stride)
;-----------------------------------------------------------------------------

INIT_MMX
cglobal pred4x4_vertical_vp8_mmxext, 3,3
    sub       r0, r2
    movd      m1, [r0-1]
    movd      m0, [r0]
    mova      m2, m0   ;t0 t1 t2 t3
    punpckldq m0, [r1] ;t0 t1 t2 t3 t4 t5 t6 t7
    lea       r1, [r0+r2*2]
    psrlq     m0, 8    ;t1 t2 t3 t4
    PRED4x4_LOWPASS m3, m1, m0, m2, m4
    movd [r0+r2*1], m3
    movd [r0+r2*2], m3
    movd [r1+r2*1], m3
    movd [r1+r2*2], m3
    RET
