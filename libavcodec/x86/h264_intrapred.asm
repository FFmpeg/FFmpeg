;******************************************************************************
;* H.264 intra prediction asm optimizations
;* Copyright (c) 2010 Jason Garrett-Glaser
;* Copyright (c) 2010 Holger Lubitz
;* Copyright (c) 2010 Loren Merritt
;* Copyright (c) 2010 Ronald S. Bultje
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

tm_shuf: times 8 db 0x03, 0x80
pw_ff00: times 8 dw 0xff00
plane_shuf:  db -8, -7, -6, -5, -4, -3, -2, -1
             db  1,  2,  3,  4,  5,  6,  7,  8
plane8_shuf: db -4, -3, -2, -1,  0,  0,  0,  0
             db  1,  2,  3,  4,  0,  0,  0,  0
pw_0to7:     dw  0,  1,  2,  3,  4,  5,  6,  7
pw_1to8:     dw  1,  2,  3,  4,  5,  6,  7,  8
pw_m8tom1:   dw -8, -7, -6, -5, -4, -3, -2, -1
pw_m4to4:    dw -4, -3, -2, -1,  1,  2,  3,  4

SECTION .text

cextern pb_1
cextern pb_3
cextern pw_4
cextern pw_5
cextern pw_8
cextern pw_16
cextern pw_17
cextern pw_32

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
; void pred16x16_plane(uint8_t *src, int stride)
;-----------------------------------------------------------------------------

%macro H264_PRED16x16_PLANE 3
cglobal pred16x16_plane_%3_%1, 2, 9, %2
    mov          r2, r1           ; +stride
    neg          r1               ; -stride

    movh         m0, [r0+r1  -1]
%if mmsize == 8
    pxor         m4, m4
    movh         m1, [r0+r1  +3 ]
    movh         m2, [r0+r1  +8 ]
    movh         m3, [r0+r1  +12]
    punpcklbw    m0, m4
    punpcklbw    m1, m4
    punpcklbw    m2, m4
    punpcklbw    m3, m4
    pmullw       m0, [pw_m8tom1  ]
    pmullw       m1, [pw_m8tom1+8]
    pmullw       m2, [pw_1to8    ]
    pmullw       m3, [pw_1to8  +8]
    paddw        m0, m2
    paddw        m1, m3
%else ; mmsize == 16
%ifidn %1, sse2
    pxor         m2, m2
    movh         m1, [r0+r1  +8]
    punpcklbw    m0, m2
    punpcklbw    m1, m2
    pmullw       m0, [pw_m8tom1]
    pmullw       m1, [pw_1to8]
    paddw        m0, m1
%else ; ssse3
    movhps       m0, [r0+r1  +8]
    pmaddubsw    m0, [plane_shuf] ; H coefficients
%endif
    movhlps      m1, m0
%endif
    paddw        m0, m1
%ifidn %1, mmx
    mova         m1, m0
    psrlq        m1, 32
%elifidn %1, mmx2
    pshufw       m1, m0, 0xE
%else ; mmsize == 16
    pshuflw      m1, m0, 0xE
%endif
    paddw        m0, m1
%ifidn %1, mmx
    mova         m1, m0
    psrlq        m1, 16
%elifidn %1, mmx2
    pshufw       m1, m0, 0x1
%else
    pshuflw      m1, m0, 0x1
%endif
    paddw        m0, m1           ; sum of H coefficients

    lea          r4, [r0+r2*8-1]
    lea          r3, [r0+r2*4-1]
    add          r4, r2

%if ARCH_X86_64
%define e_reg r8
%else
%define e_reg r0
%endif

    movzx     e_reg, byte [r3+r2*2   ]
    movzx        r5, byte [r4+r1     ]
    sub          r5, e_reg

    movzx     e_reg, byte [r3+r2     ]
    movzx        r6, byte [r4        ]
    sub          r6, e_reg
    lea          r5, [r5+r6*2]

    movzx     e_reg, byte [r3+r1     ]
    movzx        r6, byte [r4+r2*2   ]
    sub          r6, e_reg
    lea          r5, [r5+r6*4]

    movzx     e_reg, byte [r3        ]
%if ARCH_X86_64
    movzx        r7, byte [r4+r2     ]
    sub          r7, e_reg
%else
    movzx        r6, byte [r4+r2     ]
    sub          r6, e_reg
    lea          r5, [r5+r6*4]
    sub          r5, r6
%endif

    lea       e_reg, [r3+r1*4]
    lea          r3, [r4+r2*4]

    movzx        r4, byte [e_reg+r2  ]
    movzx        r6, byte [r3        ]
    sub          r6, r4
%if ARCH_X86_64
    lea          r6, [r7+r6*2]
    lea          r5, [r5+r6*2]
    add          r5, r6
%else
    lea          r5, [r5+r6*4]
    lea          r5, [r5+r6*2]
%endif

    movzx        r4, byte [e_reg     ]
%if ARCH_X86_64
    movzx        r7, byte [r3   +r2  ]
    sub          r7, r4
    sub          r5, r7
%else
    movzx        r6, byte [r3   +r2  ]
    sub          r6, r4
    lea          r5, [r5+r6*8]
    sub          r5, r6
%endif

    movzx        r4, byte [e_reg+r1  ]
    movzx        r6, byte [r3   +r2*2]
    sub          r6, r4
%if ARCH_X86_64
    add          r6, r7
%endif
    lea          r5, [r5+r6*8]

    movzx        r4, byte [e_reg+r2*2]
    movzx        r6, byte [r3   +r1  ]
    sub          r6, r4
    lea          r5, [r5+r6*4]
    add          r5, r6           ; sum of V coefficients

%if ARCH_X86_64 == 0
    mov          r0, r0m
%endif

%ifidn %3, h264
    lea          r5, [r5*5+32]
    sar          r5, 6
%elifidn %3, rv40
    lea          r5, [r5*5]
    sar          r5, 6
%elifidn %3, svq3
    test         r5, r5
    lea          r6, [r5+3]
    cmovs        r5, r6
    sar          r5, 2            ; V/4
    lea          r5, [r5*5]       ; 5*(V/4)
    test         r5, r5
    lea          r6, [r5+15]
    cmovs        r5, r6
    sar          r5, 4            ; (5*(V/4))/16
%endif

    movzx        r4, byte [r0+r1  +15]
    movzx        r3, byte [r3+r2*2   ]
    lea          r3, [r3+r4+1]
    shl          r3, 4

    movd        r1d, m0
    movsx       r1d, r1w
%ifnidn %3, svq3
%ifidn %3, h264
    lea         r1d, [r1d*5+32]
%else ; rv40
    lea         r1d, [r1d*5]
%endif
    sar         r1d, 6
%else ; svq3
    test        r1d, r1d
    lea         r4d, [r1d+3]
    cmovs       r1d, r4d
    sar         r1d, 2           ; H/4
    lea         r1d, [r1d*5]     ; 5*(H/4)
    test        r1d, r1d
    lea         r4d, [r1d+15]
    cmovs       r1d, r4d
    sar         r1d, 4           ; (5*(H/4))/16
%endif
    movd         m0, r1d

    add         r1d, r5d
    add         r3d, r1d
    shl         r1d, 3
    sub         r3d, r1d          ; a

    movd         m1, r5d
    movd         m3, r3d
%ifidn %1, mmx
    punpcklwd    m0, m0
    punpcklwd    m1, m1
    punpcklwd    m3, m3
    punpckldq    m0, m0
    punpckldq    m1, m1
    punpckldq    m3, m3
%elifidn %1, mmx2
    pshufw       m0, m0, 0x0
    pshufw       m1, m1, 0x0
    pshufw       m3, m3, 0x0
%else
    pshuflw      m0, m0, 0x0
    pshuflw      m1, m1, 0x0
    pshuflw      m3, m3, 0x0
    punpcklqdq   m0, m0           ; splat H (words)
    punpcklqdq   m1, m1           ; splat V (words)
    punpcklqdq   m3, m3           ; splat a (words)
%endif
%ifidn %3, svq3
    SWAP          0, 1
%endif
    mova         m2, m0
%if mmsize == 8
    mova         m5, m0
%endif
    pmullw       m0, [pw_0to7]    ; 0*H, 1*H, ..., 7*H  (words)
%if mmsize == 16
    psllw        m2, 3
%else
    psllw        m5, 3
    psllw        m2, 2
    mova         m6, m5
    paddw        m6, m2
%endif
    paddw        m0, m3           ; a + {0,1,2,3,4,5,6,7}*H
    paddw        m2, m0           ; a + {8,9,10,11,12,13,14,15}*H
%if mmsize == 8
    paddw        m5, m0           ; a + {8,9,10,11}*H
    paddw        m6, m0           ; a + {12,13,14,15}*H
%endif

    mov          r4, 8
.loop
    mova         m3, m0           ; b[0..7]
    mova         m4, m2           ; b[8..15]
    psraw        m3, 5
    psraw        m4, 5
    packuswb     m3, m4
    mova       [r0], m3
%if mmsize == 8
    mova         m3, m5           ; b[8..11]
    mova         m4, m6           ; b[12..15]
    psraw        m3, 5
    psraw        m4, 5
    packuswb     m3, m4
    mova     [r0+8], m3
%endif
    paddw        m0, m1
    paddw        m2, m1
%if mmsize == 8
    paddw        m5, m1
    paddw        m6, m1
%endif

    mova         m3, m0           ; b[0..7]
    mova         m4, m2           ; b[8..15]
    psraw        m3, 5
    psraw        m4, 5
    packuswb     m3, m4
    mova    [r0+r2], m3
%if mmsize == 8
    mova         m3, m5           ; b[8..11]
    mova         m4, m6           ; b[12..15]
    psraw        m3, 5
    psraw        m4, 5
    packuswb     m3, m4
    mova  [r0+r2+8], m3
%endif
    paddw        m0, m1
    paddw        m2, m1
%if mmsize == 8
    paddw        m5, m1
    paddw        m6, m1
%endif

    lea          r0, [r0+r2*2]
    dec          r4
    jg .loop
    REP_RET
%endmacro

INIT_MMX
H264_PRED16x16_PLANE mmx,   0, h264
H264_PRED16x16_PLANE mmx,   0, rv40
H264_PRED16x16_PLANE mmx,   0, svq3
H264_PRED16x16_PLANE mmx2,  0, h264
H264_PRED16x16_PLANE mmx2,  0, rv40
H264_PRED16x16_PLANE mmx2,  0, svq3
INIT_XMM
H264_PRED16x16_PLANE sse2,  8, h264
H264_PRED16x16_PLANE sse2,  8, rv40
H264_PRED16x16_PLANE sse2,  8, svq3
H264_PRED16x16_PLANE ssse3, 8, h264
H264_PRED16x16_PLANE ssse3, 8, rv40
H264_PRED16x16_PLANE ssse3, 8, svq3

;-----------------------------------------------------------------------------
; void pred8x8_plane(uint8_t *src, int stride)
;-----------------------------------------------------------------------------

%macro H264_PRED8x8_PLANE 2
cglobal pred8x8_plane_%1, 2, 9, %2
    mov          r2, r1           ; +stride
    neg          r1               ; -stride

    movd         m0, [r0+r1  -1]
%if mmsize == 8
    pxor         m2, m2
    movh         m1, [r0+r1  +4 ]
    punpcklbw    m0, m2
    punpcklbw    m1, m2
    pmullw       m0, [pw_m4to4]
    pmullw       m1, [pw_m4to4+8]
%else ; mmsize == 16
%ifidn %1, sse2
    pxor         m2, m2
    movd         m1, [r0+r1  +4]
    punpckldq    m0, m1
    punpcklbw    m0, m2
    pmullw       m0, [pw_m4to4]
%else ; ssse3
    movhps       m0, [r0+r1  +4]   ; this reads 4 bytes more than necessary
    pmaddubsw    m0, [plane8_shuf] ; H coefficients
%endif
    movhlps      m1, m0
%endif
    paddw        m0, m1

%ifnidn %1, ssse3
%ifidn %1, mmx
    mova         m1, m0
    psrlq        m1, 32
%elifidn %1, mmx2
    pshufw       m1, m0, 0xE
%else ; mmsize == 16
    pshuflw      m1, m0, 0xE
%endif
    paddw        m0, m1
%endif ; !ssse3

%ifidn %1, mmx
    mova         m1, m0
    psrlq        m1, 16
%elifidn %1, mmx2
    pshufw       m1, m0, 0x1
%else
    pshuflw      m1, m0, 0x1
%endif
    paddw        m0, m1           ; sum of H coefficients

    lea          r4, [r0+r2*4-1]
    lea          r3, [r0     -1]
    add          r4, r2

%if ARCH_X86_64
%define e_reg r8
%else
%define e_reg r0
%endif

    movzx     e_reg, byte [r3+r2*2   ]
    movzx        r5, byte [r4+r1     ]
    sub          r5, e_reg

    movzx     e_reg, byte [r3        ]
%if ARCH_X86_64
    movzx        r7, byte [r4+r2     ]
    sub          r7, e_reg
    sub          r5, r7
%else
    movzx        r6, byte [r4+r2     ]
    sub          r6, e_reg
    lea          r5, [r5+r6*4]
    sub          r5, r6
%endif

    movzx     e_reg, byte [r3+r1     ]
    movzx        r6, byte [r4+r2*2   ]
    sub          r6, e_reg
%if ARCH_X86_64
    add          r6, r7
%endif
    lea          r5, [r5+r6*4]

    movzx     e_reg, byte [r3+r2     ]
    movzx        r6, byte [r4        ]
    sub          r6, e_reg
    lea          r6, [r5+r6*2]

    lea          r5, [r6*9+16]
    lea          r5, [r5+r6*8]
    sar          r5, 5

%if ARCH_X86_64 == 0
    mov          r0, r0m
%endif

    movzx        r3, byte [r4+r2*2  ]
    movzx        r4, byte [r0+r1  +7]
    lea          r3, [r3+r4+1]
    shl          r3, 4
    movd        r1d, m0
    movsx       r1d, r1w
    imul        r1d, 17
    add         r1d, 16
    sar         r1d, 5
    movd         m0, r1d
    add         r1d, r5d
    sub         r3d, r1d
    add         r1d, r1d
    sub         r3d, r1d          ; a

    movd         m1, r5d
    movd         m3, r3d
%ifidn %1, mmx
    punpcklwd    m0, m0
    punpcklwd    m1, m1
    punpcklwd    m3, m3
    punpckldq    m0, m0
    punpckldq    m1, m1
    punpckldq    m3, m3
%elifidn %1, mmx2
    pshufw       m0, m0, 0x0
    pshufw       m1, m1, 0x0
    pshufw       m3, m3, 0x0
%else
    pshuflw      m0, m0, 0x0
    pshuflw      m1, m1, 0x0
    pshuflw      m3, m3, 0x0
    punpcklqdq   m0, m0           ; splat H (words)
    punpcklqdq   m1, m1           ; splat V (words)
    punpcklqdq   m3, m3           ; splat a (words)
%endif
%if mmsize == 8
    mova         m2, m0
%endif
    pmullw       m0, [pw_0to7]    ; 0*H, 1*H, ..., 7*H  (words)
    paddw        m0, m3           ; a + {0,1,2,3,4,5,6,7}*H
%if mmsize == 8
    psllw        m2, 2
    paddw        m2, m0           ; a + {4,5,6,7}*H
%endif

    mov          r4, 4
ALIGN 16
.loop
%if mmsize == 16
    mova         m3, m0           ; b[0..7]
    paddw        m0, m1
    psraw        m3, 5
    mova         m4, m0           ; V+b[0..7]
    paddw        m0, m1
    psraw        m4, 5
    packuswb     m3, m4
    movh       [r0], m3
    movhps  [r0+r2], m3
%else ; mmsize == 8
    mova         m3, m0           ; b[0..3]
    mova         m4, m2           ; b[4..7]
    paddw        m0, m1
    paddw        m2, m1
    psraw        m3, 5
    psraw        m4, 5
    mova         m5, m0           ; V+b[0..3]
    mova         m6, m2           ; V+b[4..7]
    paddw        m0, m1
    paddw        m2, m1
    psraw        m5, 5
    psraw        m6, 5
    packuswb     m3, m4
    packuswb     m5, m6
    mova       [r0], m3
    mova    [r0+r2], m5
%endif

    lea          r0, [r0+r2*2]
    dec          r4
    jg .loop
    REP_RET
%endmacro

INIT_MMX
H264_PRED8x8_PLANE mmx,   0
H264_PRED8x8_PLANE mmx2,  0
INIT_XMM
H264_PRED8x8_PLANE sse2,  8
H264_PRED8x8_PLANE ssse3, 8

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
; void pred8x8_top_dc_mmxext(uint8_t *src, int stride)
;-----------------------------------------------------------------------------
cglobal pred8x8_top_dc_mmxext, 2,5
    sub         r0, r1
    movq       mm0, [r0]
    pxor       mm1, mm1
    pxor       mm2, mm2
    lea         r2, [r0+r1*2]
    punpckhbw  mm1, mm0
    punpcklbw  mm0, mm2
    psadbw     mm1, mm2        ; s1
    lea         r3, [r2+r1*2]
    psadbw     mm0, mm2        ; s0
    psrlw      mm1, 1
    psrlw      mm0, 1
    pavgw      mm1, mm2
    lea         r4, [r3+r1*2]
    pavgw      mm0, mm2
    pshufw     mm1, mm1, 0
    pshufw     mm0, mm0, 0     ; dc0 (w)
    packuswb   mm0, mm1        ; dc0,dc1 (b)
    movq [r0+r1*1], mm0
    movq [r0+r1*2], mm0
    lea         r0, [r3+r1*2]
    movq [r2+r1*1], mm0
    movq [r2+r1*2], mm0
    movq [r3+r1*1], mm0
    movq [r3+r1*2], mm0
    movq [r0+r1*1], mm0
    movq [r0+r1*2], mm0
    RET

;-----------------------------------------------------------------------------
; void pred8x8_dc_mmxext(uint8_t *src, int stride)
;-----------------------------------------------------------------------------

INIT_MMX
cglobal pred8x8_dc_mmxext, 2,5
    sub       r0, r1
    pxor      m7, m7
    movd      m0, [r0+0]
    movd      m1, [r0+4]
    psadbw    m0, m7            ; s0
    mov       r4, r0
    psadbw    m1, m7            ; s1

    movzx    r2d, byte [r0+r1*1-1]
    movzx    r3d, byte [r0+r1*2-1]
    lea       r0, [r0+r1*2]
    add      r2d, r3d
    movzx    r3d, byte [r0+r1*1-1]
    add      r2d, r3d
    movzx    r3d, byte [r0+r1*2-1]
    add      r2d, r3d
    lea       r0, [r0+r1*2]
    movd      m2, r2d            ; s2
    movzx    r2d, byte [r0+r1*1-1]
    movzx    r3d, byte [r0+r1*2-1]
    lea       r0, [r0+r1*2]
    add      r2d, r3d
    movzx    r3d, byte [r0+r1*1-1]
    add      r2d, r3d
    movzx    r3d, byte [r0+r1*2-1]
    add      r2d, r3d
    movd      m3, r2d            ; s3

    punpcklwd m0, m1
    mov       r0, r4
    punpcklwd m2, m3
    punpckldq m0, m2            ; s0, s1, s2, s3
    pshufw    m3, m0, 11110110b ; s2, s1, s3, s3
    lea       r2, [r0+r1*2]
    pshufw    m0, m0, 01110100b ; s0, s1, s3, s1
    paddw     m0, m3
    lea       r3, [r2+r1*2]
    psrlw     m0, 2
    pavgw     m0, m7            ; s0+s2, s1, s3, s1+s3
    lea       r4, [r3+r1*2]
    packuswb  m0, m0
    punpcklbw m0, m0
    movq      m1, m0
    punpcklbw m0, m0
    punpckhbw m1, m1
    movq [r0+r1*1], m0
    movq [r0+r1*2], m0
    movq [r2+r1*1], m0
    movq [r2+r1*2], m0
    movq [r3+r1*1], m1
    movq [r3+r1*2], m1
    movq [r4+r1*1], m1
    movq [r4+r1*2], m1
    RET

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
; void pred8x8l_top_dc(uint8_t *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------
%macro PRED8x8L_TOP_DC 1
cglobal pred8x8l_top_dc_%1, 4,4
    sub          r0, r3
    pxor        mm7, mm7
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1 ; top_left
    jz .fix_lt_2
    test         r2, r2 ; top_right
    jz .fix_tr_1
    jmp .body
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2 ; top_right
    jnz .body
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
.body
    PRED4x4_LOWPASS mm0, mm2, mm1, mm3, mm5
    psadbw   mm7, mm0
    paddw    mm7, [pw_4]
    psrlw    mm7, 3
    pshufw   mm7, mm7, 0
    packuswb mm7, mm7
%rep 3
    movq [r0+r3*1], mm7
    movq [r0+r3*2], mm7
    lea    r0, [r0+r3*2]
%endrep
    movq [r0+r3*1], mm7
    movq [r0+r3*2], mm7
    RET
%endmacro

INIT_MMX
%define PALIGNR PALIGNR_MMX
PRED8x8L_TOP_DC mmxext
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_TOP_DC ssse3

;-----------------------------------------------------------------------------
;void pred8x8l_dc(uint8_t *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------

%macro PRED8x8L_DC 1
cglobal pred8x8l_dc_%1, 4,5
    sub          r0, r3
    lea          r4, [r0+r3*2]
    movq        mm0, [r0+r3*1-8]
    punpckhbw   mm0, [r0+r3*0-8]
    movq        mm1, [r4+r3*1-8]
    punpckhbw   mm1, [r0+r3*2-8]
    mov          r4, r0
    punpckhwd   mm1, mm0
    lea          r0, [r0+r3*4]
    movq        mm2, [r0+r3*1-8]
    punpckhbw   mm2, [r0+r3*0-8]
    lea          r0, [r0+r3*2]
    movq        mm3, [r0+r3*1-8]
    punpckhbw   mm3, [r0+r3*0-8]
    punpckhwd   mm3, mm2
    punpckhdq   mm3, mm1
    lea          r0, [r0+r3*2]
    movq        mm0, [r0+r3*0-8]
    movq        mm1, [r4]
    mov          r0, r4
    movq        mm4, mm3
    movq        mm2, mm3
    PALIGNR     mm4, mm0, 7, mm0
    PALIGNR     mm1, mm2, 1, mm2
    test        r1, r1
    jnz .do_left
.fix_lt_1:
    movq        mm5, mm3
    pxor        mm5, mm4
    psrlq       mm5, 56
    psllq       mm5, 48
    pxor        mm1, mm5
    jmp .do_left
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2
    jnz .body
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
    jmp .body
.do_left:
    movq        mm0, mm4
    PRED4x4_LOWPASS mm2, mm1, mm4, mm3, mm5
    movq        mm4, mm0
    movq        mm7, mm2
    PRED4x4_LOWPASS mm1, mm3, mm0, mm4, mm5
    psllq       mm1, 56
    PALIGNR     mm7, mm1, 7, mm3
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1
    jz .fix_lt_2
    test         r2, r2
    jz .fix_tr_1
.body
    lea          r1, [r0+r3*2]
    PRED4x4_LOWPASS mm6, mm2, mm1, mm3, mm5
    pxor        mm0, mm0
    pxor        mm1, mm1
    lea          r2, [r1+r3*2]
    psadbw      mm0, mm7
    psadbw      mm1, mm6
    paddw       mm0, [pw_8]
    paddw       mm0, mm1
    lea          r4, [r2+r3*2]
    psrlw       mm0, 4
    pshufw      mm0, mm0, 0
    packuswb    mm0, mm0
    movq [r0+r3*1], mm0
    movq [r0+r3*2], mm0
    movq [r1+r3*1], mm0
    movq [r1+r3*2], mm0
    movq [r2+r3*1], mm0
    movq [r2+r3*2], mm0
    movq [r4+r3*1], mm0
    movq [r4+r3*2], mm0
    RET
%endmacro
INIT_MMX
%define PALIGNR PALIGNR_MMX
PRED8x8L_DC mmxext
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_DC ssse3

;-----------------------------------------------------------------------------
; void pred8x8l_horizontal(uint8_t *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------

%macro PRED8x8L_HORIZONTAL 1
cglobal pred8x8l_horizontal_%1, 4,4
    sub          r0, r3
    lea          r2, [r0+r3*2]
    movq        mm0, [r0+r3*1-8]
    test         r1, r1
    lea          r1, [r0+r3]
    cmovnz       r1, r0
    punpckhbw   mm0, [r1+r3*0-8]
    movq        mm1, [r2+r3*1-8]
    punpckhbw   mm1, [r0+r3*2-8]
    mov          r2, r0
    punpckhwd   mm1, mm0
    lea          r0, [r0+r3*4]
    movq        mm2, [r0+r3*1-8]
    punpckhbw   mm2, [r0+r3*0-8]
    lea          r0, [r0+r3*2]
    movq        mm3, [r0+r3*1-8]
    punpckhbw   mm3, [r0+r3*0-8]
    punpckhwd   mm3, mm2
    punpckhdq   mm3, mm1
    lea          r0, [r0+r3*2]
    movq        mm0, [r0+r3*0-8]
    movq        mm1, [r1+r3*0-8]
    mov          r0, r2
    movq        mm4, mm3
    movq        mm2, mm3
    PALIGNR     mm4, mm0, 7, mm0
    PALIGNR     mm1, mm2, 1, mm2
    movq        mm0, mm4
    PRED4x4_LOWPASS mm2, mm1, mm4, mm3, mm5
    movq        mm4, mm0
    movq        mm7, mm2
    PRED4x4_LOWPASS mm1, mm3, mm0, mm4, mm5
    psllq       mm1, 56
    PALIGNR     mm7, mm1, 7, mm3
    movq        mm3, mm7
    lea         r1, [r0+r3*2]
    movq       mm7, mm3
    punpckhbw  mm3, mm3
    punpcklbw  mm7, mm7
    pshufw     mm0, mm3, 0xff
    pshufw     mm1, mm3, 0xaa
    lea         r2, [r1+r3*2]
    pshufw     mm2, mm3, 0x55
    pshufw     mm3, mm3, 0x00
    pshufw     mm4, mm7, 0xff
    pshufw     mm5, mm7, 0xaa
    pshufw     mm6, mm7, 0x55
    pshufw     mm7, mm7, 0x00
    movq [r0+r3*1], mm0
    movq [r0+r3*2], mm1
    movq [r1+r3*1], mm2
    movq [r1+r3*2], mm3
    movq [r2+r3*1], mm4
    movq [r2+r3*2], mm5
    lea         r0, [r2+r3*2]
    movq [r0+r3*1], mm6
    movq [r0+r3*2], mm7
    RET
%endmacro

INIT_MMX
%define PALIGNR PALIGNR_MMX
PRED8x8L_HORIZONTAL mmxext
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_HORIZONTAL ssse3

;-----------------------------------------------------------------------------
; void pred8x8l_vertical(uint8_t *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------

%macro PRED8x8L_VERTICAL 1
cglobal pred8x8l_vertical_%1, 4,4
    sub          r0, r3
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1 ; top_left
    jz .fix_lt_2
    test         r2, r2 ; top_right
    jz .fix_tr_1
    jmp .body
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2 ; top_right
    jnz .body
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
.body
    PRED4x4_LOWPASS mm0, mm2, mm1, mm3, mm5
%rep 3
    movq [r0+r3*1], mm0
    movq [r0+r3*2], mm0
    lea    r0, [r0+r3*2]
%endrep
    movq [r0+r3*1], mm0
    movq [r0+r3*2], mm0
    RET
%endmacro

INIT_MMX
%define PALIGNR PALIGNR_MMX
PRED8x8L_VERTICAL mmxext
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_VERTICAL ssse3

;-----------------------------------------------------------------------------
;void pred8x8l_down_left(uint8_t *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------

INIT_MMX
%define PALIGNR PALIGNR_MMX
cglobal pred8x8l_down_left_mmxext, 4,5
    sub          r0, r3
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1
    jz .fix_lt_2
    test         r2, r2
    jz .fix_tr_1
    jmp .do_top
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2
    jnz .do_top
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
    jmp .do_top
.fix_tr_2:
    punpckhbw   mm3, mm3
    pshufw      mm1, mm3, 0xFF
    jmp .do_topright
.do_top:
    PRED4x4_LOWPASS mm4, mm2, mm1, mm3, mm5
    movq        mm7, mm4
    test         r2, r2
    jz .fix_tr_2
    movq        mm0, [r0+8]
    movq        mm5, mm0
    movq        mm2, mm0
    movq        mm4, mm0
    psrlq       mm5, 56
    PALIGNR     mm2, mm3, 7, mm3
    PALIGNR     mm5, mm4, 1, mm4
    PRED4x4_LOWPASS mm1, mm2, mm5, mm0, mm4
.do_topright:
    lea          r1, [r0+r3*2]
    movq        mm6, mm1
    psrlq       mm1, 56
    movq        mm4, mm1
    lea          r2, [r1+r3*2]
    movq        mm2, mm6
    PALIGNR     mm2, mm7, 1, mm0
    movq        mm3, mm6
    PALIGNR     mm3, mm7, 7, mm0
    PALIGNR     mm4, mm6, 1, mm0
    movq        mm5, mm7
    movq        mm1, mm7
    movq        mm7, mm6
    lea          r4, [r2+r3*2]
    psllq       mm1, 8
    PRED4x4_LOWPASS mm0, mm1, mm2, mm5, mm6
    PRED4x4_LOWPASS mm1, mm3, mm4, mm7, mm6
    movq  [r4+r3*2], mm1
    movq        mm2, mm0
    psllq       mm1, 8
    psrlq       mm2, 56
    psllq       mm0, 8
    por         mm1, mm2
    movq  [r4+r3*1], mm1
    movq        mm2, mm0
    psllq       mm1, 8
    psrlq       mm2, 56
    psllq       mm0, 8
    por         mm1, mm2
    movq  [r2+r3*2], mm1
    movq        mm2, mm0
    psllq       mm1, 8
    psrlq       mm2, 56
    psllq       mm0, 8
    por         mm1, mm2
    movq  [r2+r3*1], mm1
    movq        mm2, mm0
    psllq       mm1, 8
    psrlq       mm2, 56
    psllq       mm0, 8
    por         mm1, mm2
    movq  [r1+r3*2], mm1
    movq        mm2, mm0
    psllq       mm1, 8
    psrlq       mm2, 56
    psllq       mm0, 8
    por         mm1, mm2
    movq  [r1+r3*1], mm1
    movq        mm2, mm0
    psllq       mm1, 8
    psrlq       mm2, 56
    psllq       mm0, 8
    por         mm1, mm2
    movq  [r0+r3*2], mm1
    psllq       mm1, 8
    psrlq       mm0, 56
    por         mm1, mm0
    movq  [r0+r3*1], mm1
    RET

%macro PRED8x8L_DOWN_LEFT 1
cglobal pred8x8l_down_left_%1, 4,4
    sub          r0, r3
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1 ; top_left
    jz .fix_lt_2
    test         r2, r2 ; top_right
    jz .fix_tr_1
    jmp .do_top
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2 ; top_right
    jnz .do_top
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
    jmp .do_top
.fix_tr_2:
    punpckhbw   mm3, mm3
    pshufw      mm1, mm3, 0xFF
    jmp .do_topright
.do_top:
    PRED4x4_LOWPASS mm4, mm2, mm1, mm3, mm5
    movq2dq    xmm3, mm4
    test         r2, r2 ; top_right
    jz .fix_tr_2
    movq        mm0, [r0+8]
    movq        mm5, mm0
    movq        mm2, mm0
    movq        mm4, mm0
    psrlq       mm5, 56
    PALIGNR     mm2, mm3, 7, mm3
    PALIGNR     mm5, mm4, 1, mm4
    PRED4x4_LOWPASS mm1, mm2, mm5, mm0, mm4
.do_topright:
    movq2dq    xmm4, mm1
    psrlq       mm1, 56
    movq2dq    xmm5, mm1
    lea         r1, [r0+r3*2]
    pslldq    xmm4, 8
    por       xmm3, xmm4
    movdqa    xmm2, xmm3
    psrldq    xmm2, 1
    pslldq    xmm5, 15
    por       xmm2, xmm5
    lea         r2, [r1+r3*2]
    movdqa    xmm1, xmm3
    pslldq    xmm1, 1
INIT_XMM
    PRED4x4_LOWPASS xmm0, xmm1, xmm2, xmm3, xmm4
    psrldq    xmm0, 1
    movq [r0+r3*1], xmm0
    psrldq    xmm0, 1
    movq [r0+r3*2], xmm0
    psrldq    xmm0, 1
    lea         r0, [r2+r3*2]
    movq [r1+r3*1], xmm0
    psrldq    xmm0, 1
    movq [r1+r3*2], xmm0
    psrldq    xmm0, 1
    movq [r2+r3*1], xmm0
    psrldq    xmm0, 1
    movq [r2+r3*2], xmm0
    psrldq    xmm0, 1
    movq [r0+r3*1], xmm0
    psrldq    xmm0, 1
    movq [r0+r3*2], xmm0
    RET
%endmacro

INIT_MMX
%define PALIGNR PALIGNR_MMX
PRED8x8L_DOWN_LEFT sse2
INIT_MMX
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_DOWN_LEFT ssse3

;-----------------------------------------------------------------------------
;void pred8x8l_down_right_mmxext(uint8_t *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------

INIT_MMX
%define PALIGNR PALIGNR_MMX
cglobal pred8x8l_down_right_mmxext, 4,5
    sub          r0, r3
    lea          r4, [r0+r3*2]
    movq        mm0, [r0+r3*1-8]
    punpckhbw   mm0, [r0+r3*0-8]
    movq        mm1, [r4+r3*1-8]
    punpckhbw   mm1, [r0+r3*2-8]
    mov          r4, r0
    punpckhwd   mm1, mm0
    lea          r0, [r0+r3*4]
    movq        mm2, [r0+r3*1-8]
    punpckhbw   mm2, [r0+r3*0-8]
    lea          r0, [r0+r3*2]
    movq        mm3, [r0+r3*1-8]
    punpckhbw   mm3, [r0+r3*0-8]
    punpckhwd   mm3, mm2
    punpckhdq   mm3, mm1
    lea          r0, [r0+r3*2]
    movq        mm0, [r0+r3*0-8]
    movq        mm1, [r4]
    mov          r0, r4
    movq        mm4, mm3
    movq        mm2, mm3
    PALIGNR     mm4, mm0, 7, mm0
    PALIGNR     mm1, mm2, 1, mm2
    test        r1, r1 ; top_left
    jz .fix_lt_1
.do_left:
    movq        mm0, mm4
    PRED4x4_LOWPASS mm2, mm1, mm4, mm3, mm5
    movq        mm4, mm0
    movq        mm7, mm2
    movq        mm6, mm2
    PRED4x4_LOWPASS mm1, mm3, mm0, mm4, mm5
    psllq       mm1, 56
    PALIGNR     mm7, mm1, 7, mm3
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1 ; top_left
    jz .fix_lt_2
    test         r2, r2 ; top_right
    jz .fix_tr_1
.do_top:
    PRED4x4_LOWPASS mm4, mm2, mm1, mm3, mm5
    movq        mm5, mm4
    jmp .body
.fix_lt_1:
    movq        mm5, mm3
    pxor        mm5, mm4
    psrlq       mm5, 56
    psllq       mm5, 48
    pxor        mm1, mm5
    jmp .do_left
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2 ; top_right
    jnz .do_top
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
    jmp .do_top
.body
    lea         r1, [r0+r3*2]
    movq       mm1, mm7
    movq       mm7, mm5
    movq       mm5, mm6
    movq       mm2, mm7
    lea         r2, [r1+r3*2]
    PALIGNR    mm2, mm6, 1, mm0
    movq       mm3, mm7
    PALIGNR    mm3, mm6, 7, mm0
    movq       mm4, mm7
    lea         r4, [r2+r3*2]
    psrlq      mm4, 8
    PRED4x4_LOWPASS mm0, mm1, mm2, mm5, mm6
    PRED4x4_LOWPASS mm1, mm3, mm4, mm7, mm6
    movq [r4+r3*2], mm0
    movq       mm2, mm1
    psrlq      mm0, 8
    psllq      mm2, 56
    psrlq      mm1, 8
    por        mm0, mm2
    movq [r4+r3*1], mm0
    movq       mm2, mm1
    psrlq      mm0, 8
    psllq      mm2, 56
    psrlq      mm1, 8
    por        mm0, mm2
    movq [r2+r3*2], mm0
    movq       mm2, mm1
    psrlq      mm0, 8
    psllq      mm2, 56
    psrlq      mm1, 8
    por        mm0, mm2
    movq [r2+r3*1], mm0
    movq       mm2, mm1
    psrlq      mm0, 8
    psllq      mm2, 56
    psrlq      mm1, 8
    por        mm0, mm2
    movq [r1+r3*2], mm0
    movq       mm2, mm1
    psrlq      mm0, 8
    psllq      mm2, 56
    psrlq      mm1, 8
    por        mm0, mm2
    movq [r1+r3*1], mm0
    movq       mm2, mm1
    psrlq      mm0, 8
    psllq      mm2, 56
    psrlq      mm1, 8
    por        mm0, mm2
    movq [r0+r3*2], mm0
    psrlq      mm0, 8
    psllq      mm1, 56
    por        mm0, mm1
    movq [r0+r3*1], mm0
    RET

%macro PRED8x8L_DOWN_RIGHT 1
cglobal pred8x8l_down_right_%1, 4,5
    sub          r0, r3
    lea          r4, [r0+r3*2]
    movq        mm0, [r0+r3*1-8]
    punpckhbw   mm0, [r0+r3*0-8]
    movq        mm1, [r4+r3*1-8]
    punpckhbw   mm1, [r0+r3*2-8]
    mov          r4, r0
    punpckhwd   mm1, mm0
    lea          r0, [r0+r3*4]
    movq        mm2, [r0+r3*1-8]
    punpckhbw   mm2, [r0+r3*0-8]
    lea          r0, [r0+r3*2]
    movq        mm3, [r0+r3*1-8]
    punpckhbw   mm3, [r0+r3*0-8]
    punpckhwd   mm3, mm2
    punpckhdq   mm3, mm1
    lea          r0, [r0+r3*2]
    movq        mm0, [r0+r3*0-8]
    movq        mm1, [r4]
    mov          r0, r4
    movq        mm4, mm3
    movq        mm2, mm3
    PALIGNR     mm4, mm0, 7, mm0
    PALIGNR     mm1, mm2, 1, mm2
    test        r1, r1
    jz .fix_lt_1
    jmp .do_left
.fix_lt_1:
    movq        mm5, mm3
    pxor        mm5, mm4
    psrlq       mm5, 56
    psllq       mm5, 48
    pxor        mm1, mm5
    jmp .do_left
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2
    jnz .do_top
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
    jmp .do_top
.do_left:
    movq        mm0, mm4
    PRED4x4_LOWPASS mm2, mm1, mm4, mm3, mm5
    movq        mm4, mm0
    movq        mm7, mm2
    movq2dq    xmm3, mm2
    PRED4x4_LOWPASS mm1, mm3, mm0, mm4, mm5
    psllq       mm1, 56
    PALIGNR     mm7, mm1, 7, mm3
    movq2dq    xmm1, mm7
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1
    jz .fix_lt_2
    test         r2, r2
    jz .fix_tr_1
.do_top:
    PRED4x4_LOWPASS mm4, mm2, mm1, mm3, mm5
    movq2dq   xmm4, mm4
    lea         r1, [r0+r3*2]
    movdqa    xmm0, xmm3
    pslldq    xmm4, 8
    por       xmm3, xmm4
    lea         r2, [r1+r3*2]
    pslldq    xmm4, 1
    por       xmm1, xmm4
    psrldq    xmm0, 7
    pslldq    xmm0, 15
    psrldq    xmm0, 7
    por       xmm1, xmm0
    lea         r0, [r2+r3*2]
    movdqa    xmm2, xmm3
    psrldq    xmm2, 1
INIT_XMM
    PRED4x4_LOWPASS xmm0, xmm1, xmm2, xmm3, xmm4
    movdqa    xmm1, xmm0
    psrldq    xmm1, 1
    movq [r0+r3*2], xmm0
    movq [r0+r3*1], xmm1
    psrldq    xmm0, 2
    psrldq    xmm1, 2
    movq [r2+r3*2], xmm0
    movq [r2+r3*1], xmm1
    psrldq    xmm0, 2
    psrldq    xmm1, 2
    movq [r1+r3*2], xmm0
    movq [r1+r3*1], xmm1
    psrldq    xmm0, 2
    psrldq    xmm1, 2
    movq [r4+r3*2], xmm0
    movq [r4+r3*1], xmm1
    RET
%endmacro

INIT_MMX
%define PALIGNR PALIGNR_MMX
PRED8x8L_DOWN_RIGHT sse2
INIT_MMX
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_DOWN_RIGHT ssse3

;-----------------------------------------------------------------------------
; void pred8x8l_vertical_right(uint8_t *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------

INIT_MMX
%define PALIGNR PALIGNR_MMX
cglobal pred8x8l_vertical_right_mmxext, 4,5
    sub          r0, r3
    lea          r4, [r0+r3*2]
    movq        mm0, [r0+r3*1-8]
    punpckhbw   mm0, [r0+r3*0-8]
    movq        mm1, [r4+r3*1-8]
    punpckhbw   mm1, [r0+r3*2-8]
    mov          r4, r0
    punpckhwd   mm1, mm0
    lea          r0, [r0+r3*4]
    movq        mm2, [r0+r3*1-8]
    punpckhbw   mm2, [r0+r3*0-8]
    lea          r0, [r0+r3*2]
    movq        mm3, [r0+r3*1-8]
    punpckhbw   mm3, [r0+r3*0-8]
    punpckhwd   mm3, mm2
    punpckhdq   mm3, mm1
    lea          r0, [r0+r3*2]
    movq        mm0, [r0+r3*0-8]
    movq        mm1, [r4]
    mov          r0, r4
    movq        mm4, mm3
    movq        mm2, mm3
    PALIGNR     mm4, mm0, 7, mm0
    PALIGNR     mm1, mm2, 1, mm2
    test        r1, r1
    jz .fix_lt_1
    jmp .do_left
.fix_lt_1:
    movq        mm5, mm3
    pxor        mm5, mm4
    psrlq       mm5, 56
    psllq       mm5, 48
    pxor        mm1, mm5
    jmp .do_left
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2
    jnz .do_top
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
    jmp .do_top
.do_left:
    movq        mm0, mm4
    PRED4x4_LOWPASS mm2, mm1, mm4, mm3, mm5
    movq        mm7, mm2
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1
    jz .fix_lt_2
    test         r2, r2
    jz .fix_tr_1
.do_top
    PRED4x4_LOWPASS mm6, mm2, mm1, mm3, mm5
    lea         r1, [r0+r3*2]
    movq       mm2, mm6
    movq       mm3, mm6
    PALIGNR    mm3, mm7, 7, mm0
    PALIGNR    mm6, mm7, 6, mm1
    movq       mm4, mm3
    pavgb      mm3, mm2
    lea         r2, [r1+r3*2]
    PRED4x4_LOWPASS mm0, mm6, mm2, mm4, mm5
    movq [r0+r3*1], mm3
    movq [r0+r3*2], mm0
    movq       mm5, mm0
    movq       mm6, mm3
    movq       mm1, mm7
    movq       mm2, mm1
    psllq      mm2, 8
    movq       mm3, mm1
    psllq      mm3, 16
    lea         r4, [r2+r3*2]
    PRED4x4_LOWPASS mm0, mm1, mm3, mm2, mm4
    PALIGNR    mm6, mm0, 7, mm2
    movq [r1+r3*1], mm6
    psllq      mm0, 8
    PALIGNR    mm5, mm0, 7, mm1
    movq [r1+r3*2], mm5
    psllq      mm0, 8
    PALIGNR    mm6, mm0, 7, mm2
    movq [r2+r3*1], mm6
    psllq      mm0, 8
    PALIGNR    mm5, mm0, 7, mm1
    movq [r2+r3*2], mm5
    psllq      mm0, 8
    PALIGNR    mm6, mm0, 7, mm2
    movq [r4+r3*1], mm6
    psllq      mm0, 8
    PALIGNR    mm5, mm0, 7, mm1
    movq [r4+r3*2], mm5
    RET

%macro PRED8x8L_VERTICAL_RIGHT 1
cglobal pred8x8l_vertical_right_%1, 4,5,7
    ; manually spill XMM registers for Win64 because
    ; the code here is initialized with INIT_MMX
    WIN64_SPILL_XMM 7
    sub          r0, r3
    lea          r4, [r0+r3*2]
    movq        mm0, [r0+r3*1-8]
    punpckhbw   mm0, [r0+r3*0-8]
    movq        mm1, [r4+r3*1-8]
    punpckhbw   mm1, [r0+r3*2-8]
    mov          r4, r0
    punpckhwd   mm1, mm0
    lea          r0, [r0+r3*4]
    movq        mm2, [r0+r3*1-8]
    punpckhbw   mm2, [r0+r3*0-8]
    lea          r0, [r0+r3*2]
    movq        mm3, [r0+r3*1-8]
    punpckhbw   mm3, [r0+r3*0-8]
    punpckhwd   mm3, mm2
    punpckhdq   mm3, mm1
    lea          r0, [r0+r3*2]
    movq        mm0, [r0+r3*0-8]
    movq        mm1, [r4]
    mov          r0, r4
    movq        mm4, mm3
    movq        mm2, mm3
    PALIGNR     mm4, mm0, 7, mm0
    PALIGNR     mm1, mm2, 1, mm2
    test        r1, r1
    jnz .do_left
.fix_lt_1:
    movq        mm5, mm3
    pxor        mm5, mm4
    psrlq       mm5, 56
    psllq       mm5, 48
    pxor        mm1, mm5
    jmp .do_left
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2
    jnz .do_top
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
    jmp .do_top
.do_left:
    movq        mm0, mm4
    PRED4x4_LOWPASS mm2, mm1, mm4, mm3, mm5
    movq2dq    xmm0, mm2
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1
    jz .fix_lt_2
    test         r2, r2
    jz .fix_tr_1
.do_top
    PRED4x4_LOWPASS mm6, mm2, mm1, mm3, mm5
    lea           r1, [r0+r3*2]
    movq2dq     xmm4, mm6
    pslldq      xmm4, 8
    por         xmm0, xmm4
    movdqa      xmm6, [pw_ff00]
    movdqa      xmm1, xmm0
    lea           r2, [r1+r3*2]
    movdqa      xmm2, xmm0
    movdqa      xmm3, xmm0
    pslldq      xmm0, 1
    pslldq      xmm1, 2
    pavgb       xmm2, xmm0
INIT_XMM
    PRED4x4_LOWPASS xmm4, xmm3, xmm1, xmm0, xmm5
    pandn       xmm6, xmm4
    movdqa      xmm5, xmm4
    psrlw       xmm4, 8
    packuswb    xmm6, xmm4
    movhlps     xmm4, xmm6
    movhps [r0+r3*2], xmm5
    movhps [r0+r3*1], xmm2
    psrldq      xmm5, 4
    movss       xmm5, xmm6
    psrldq      xmm2, 4
    movss       xmm2, xmm4
    lea           r0, [r2+r3*2]
    psrldq      xmm5, 1
    psrldq      xmm2, 1
    movq        [r0+r3*2], xmm5
    movq        [r0+r3*1], xmm2
    psrldq      xmm5, 1
    psrldq      xmm2, 1
    movq        [r2+r3*2], xmm5
    movq        [r2+r3*1], xmm2
    psrldq      xmm5, 1
    psrldq      xmm2, 1
    movq        [r1+r3*2], xmm5
    movq        [r1+r3*1], xmm2
    RET
%endmacro

INIT_MMX
%define PALIGNR PALIGNR_MMX
PRED8x8L_VERTICAL_RIGHT sse2
INIT_MMX
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_VERTICAL_RIGHT ssse3

;-----------------------------------------------------------------------------
;void pred8x8l_vertical_left(uint8_t *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------

%macro PRED8x8L_VERTICAL_LEFT 1
cglobal pred8x8l_vertical_left_%1, 4,4
    sub          r0, r3
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1
    jz .fix_lt_2
    test         r2, r2
    jz .fix_tr_1
    jmp .do_top
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2
    jnz .do_top
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
    jmp .do_top
.fix_tr_2:
    punpckhbw   mm3, mm3
    pshufw      mm1, mm3, 0xFF
    jmp .do_topright
.do_top:
    PRED4x4_LOWPASS mm4, mm2, mm1, mm3, mm5
    movq2dq    xmm4, mm4
    test         r2, r2
    jz .fix_tr_2
    movq        mm0, [r0+8]
    movq        mm5, mm0
    movq        mm2, mm0
    movq        mm4, mm0
    psrlq       mm5, 56
    PALIGNR     mm2, mm3, 7, mm3
    PALIGNR     mm5, mm4, 1, mm4
    PRED4x4_LOWPASS mm1, mm2, mm5, mm0, mm4
.do_topright:
    movq2dq   xmm3, mm1
    lea         r1, [r0+r3*2]
    pslldq    xmm3, 8
    por       xmm4, xmm3
    movdqa    xmm2, xmm4
    movdqa    xmm1, xmm4
    movdqa    xmm3, xmm4
    psrldq    xmm2, 1
    pslldq    xmm1, 1
    pavgb     xmm3, xmm2
    lea         r2, [r1+r3*2]
INIT_XMM
    PRED4x4_LOWPASS xmm0, xmm1, xmm2, xmm4, xmm5
    psrldq    xmm0, 1
    movq [r0+r3*1], xmm3
    movq [r0+r3*2], xmm0
    lea         r0, [r2+r3*2]
    psrldq    xmm3, 1
    psrldq    xmm0, 1
    movq [r1+r3*1], xmm3
    movq [r1+r3*2], xmm0
    psrldq    xmm3, 1
    psrldq    xmm0, 1
    movq [r2+r3*1], xmm3
    movq [r2+r3*2], xmm0
    psrldq    xmm3, 1
    psrldq    xmm0, 1
    movq [r0+r3*1], xmm3
    movq [r0+r3*2], xmm0
    RET
%endmacro

INIT_MMX
%define PALIGNR PALIGNR_MMX
PRED8x8L_VERTICAL_LEFT sse2
%define PALIGNR PALIGNR_SSSE3
INIT_MMX
PRED8x8L_VERTICAL_LEFT ssse3

;-----------------------------------------------------------------------------
; void pred8x8l_horizontal_up(uint8_t *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------

%macro PRED8x8L_HORIZONTAL_UP 1
cglobal pred8x8l_horizontal_up_%1, 4,4
    sub          r0, r3
    lea          r2, [r0+r3*2]
    movq        mm0, [r0+r3*1-8]
    test         r1, r1
    lea          r1, [r0+r3]
    cmovnz       r1, r0
    punpckhbw   mm0, [r1+r3*0-8]
    movq        mm1, [r2+r3*1-8]
    punpckhbw   mm1, [r0+r3*2-8]
    mov          r2, r0
    punpckhwd   mm1, mm0
    lea          r0, [r0+r3*4]
    movq        mm2, [r0+r3*1-8]
    punpckhbw   mm2, [r0+r3*0-8]
    lea          r0, [r0+r3*2]
    movq        mm3, [r0+r3*1-8]
    punpckhbw   mm3, [r0+r3*0-8]
    punpckhwd   mm3, mm2
    punpckhdq   mm3, mm1
    lea          r0, [r0+r3*2]
    movq        mm0, [r0+r3*0-8]
    movq        mm1, [r1+r3*0-8]
    mov          r0, r2
    movq        mm4, mm3
    movq        mm2, mm3
    PALIGNR     mm4, mm0, 7, mm0
    PALIGNR     mm1, mm2, 1, mm2
    movq       mm0, mm4
    PRED4x4_LOWPASS mm2, mm1, mm4, mm3, mm5
    movq       mm4, mm0
    movq       mm7, mm2
    PRED4x4_LOWPASS mm1, mm3, mm0, mm4, mm5
    psllq      mm1, 56
    PALIGNR    mm7, mm1, 7, mm3
    lea         r1, [r0+r3*2]
    pshufw     mm0, mm7, 00011011b ; l6 l7 l4 l5 l2 l3 l0 l1
    psllq      mm7, 56             ; l7 .. .. .. .. .. .. ..
    movq       mm2, mm0
    psllw      mm0, 8
    psrlw      mm2, 8
    por        mm2, mm0            ; l7 l6 l5 l4 l3 l2 l1 l0
    movq       mm3, mm2
    movq       mm4, mm2
    movq       mm5, mm2
    psrlq      mm2, 8
    psrlq      mm3, 16
    lea         r2, [r1+r3*2]
    por        mm2, mm7            ; l7 l7 l6 l5 l4 l3 l2 l1
    punpckhbw  mm7, mm7
    por        mm3, mm7            ; l7 l7 l7 l6 l5 l4 l3 l2
    pavgb      mm4, mm2
    PRED4x4_LOWPASS mm1, mm3, mm5, mm2, mm6
    movq       mm5, mm4
    punpcklbw  mm4, mm1            ; p4 p3 p2 p1
    punpckhbw  mm5, mm1            ; p8 p7 p6 p5
    movq       mm6, mm5
    movq       mm7, mm5
    movq       mm0, mm5
    PALIGNR    mm5, mm4, 2, mm1
    pshufw     mm1, mm6, 11111001b
    PALIGNR    mm6, mm4, 4, mm2
    pshufw     mm2, mm7, 11111110b
    PALIGNR    mm7, mm4, 6, mm3
    pshufw     mm3, mm0, 11111111b
    movq [r0+r3*1], mm4
    movq [r0+r3*2], mm5
    lea         r0, [r2+r3*2]
    movq [r1+r3*1], mm6
    movq [r1+r3*2], mm7
    movq [r2+r3*1], mm0
    movq [r2+r3*2], mm1
    movq [r0+r3*1], mm2
    movq [r0+r3*2], mm3
    RET
%endmacro

INIT_MMX
%define PALIGNR PALIGNR_MMX
PRED8x8L_HORIZONTAL_UP mmxext
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_HORIZONTAL_UP ssse3

;-----------------------------------------------------------------------------
;void pred8x8l_horizontal_down(uint8_t *src, int has_topleft, int has_topright, int stride)
;-----------------------------------------------------------------------------

INIT_MMX
%define PALIGNR PALIGNR_MMX
cglobal pred8x8l_horizontal_down_mmxext, 4,5
    sub          r0, r3
    lea          r4, [r0+r3*2]
    movq        mm0, [r0+r3*1-8]
    punpckhbw   mm0, [r0+r3*0-8]
    movq        mm1, [r4+r3*1-8]
    punpckhbw   mm1, [r0+r3*2-8]
    mov          r4, r0
    punpckhwd   mm1, mm0
    lea          r0, [r0+r3*4]
    movq        mm2, [r0+r3*1-8]
    punpckhbw   mm2, [r0+r3*0-8]
    lea          r0, [r0+r3*2]
    movq        mm3, [r0+r3*1-8]
    punpckhbw   mm3, [r0+r3*0-8]
    punpckhwd   mm3, mm2
    punpckhdq   mm3, mm1
    lea          r0, [r0+r3*2]
    movq        mm0, [r0+r3*0-8]
    movq        mm1, [r4]
    mov          r0, r4
    movq        mm4, mm3
    movq        mm2, mm3
    PALIGNR     mm4, mm0, 7, mm0
    PALIGNR     mm1, mm2, 1, mm2
    test        r1, r1
    jnz .do_left
.fix_lt_1:
    movq        mm5, mm3
    pxor        mm5, mm4
    psrlq       mm5, 56
    psllq       mm5, 48
    pxor        mm1, mm5
    jmp .do_left
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2
    jnz .do_top
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
    jmp .do_top
.do_left:
    movq        mm0, mm4
    PRED4x4_LOWPASS mm2, mm1, mm4, mm3, mm5
    movq        mm4, mm0
    movq        mm7, mm2
    movq        mm6, mm2
    PRED4x4_LOWPASS mm1, mm3, mm0, mm4, mm5
    psllq       mm1, 56
    PALIGNR     mm7, mm1, 7, mm3
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1
    jz .fix_lt_2
    test         r2, r2
    jz .fix_tr_1
.do_top:
    PRED4x4_LOWPASS mm4, mm2, mm1, mm3, mm5
    movq       mm5, mm4
    lea         r1, [r0+r3*2]
    psllq      mm7, 56
    movq       mm2, mm5
    movq       mm3, mm6
    movq       mm4, mm2
    PALIGNR    mm2, mm6, 7, mm5
    PALIGNR    mm6, mm7, 7, mm0
    lea         r2, [r1+r3*2]
    PALIGNR    mm4, mm3, 1, mm7
    movq       mm5, mm3
    pavgb      mm3, mm6
    PRED4x4_LOWPASS mm0, mm4, mm6, mm5, mm7
    movq       mm4, mm2
    movq       mm1, mm2
    lea         r4, [r2+r3*2]
    psrlq      mm4, 16
    psrlq      mm1, 8
    PRED4x4_LOWPASS mm6, mm4, mm2, mm1, mm5
    movq       mm7, mm3
    punpcklbw  mm3, mm0
    punpckhbw  mm7, mm0
    movq       mm1, mm7
    movq       mm0, mm7
    movq       mm4, mm7
    movq [r4+r3*2], mm3
    PALIGNR    mm7, mm3, 2, mm5
    movq [r4+r3*1], mm7
    PALIGNR    mm1, mm3, 4, mm5
    movq [r2+r3*2], mm1
    PALIGNR    mm0, mm3, 6, mm3
    movq [r2+r3*1], mm0
    movq       mm2, mm6
    movq       mm3, mm6
    movq [r1+r3*2], mm4
    PALIGNR    mm6, mm4, 2, mm5
    movq [r1+r3*1], mm6
    PALIGNR    mm2, mm4, 4, mm5
    movq [r0+r3*2], mm2
    PALIGNR    mm3, mm4, 6, mm4
    movq [r0+r3*1], mm3
    RET

%macro PRED8x8L_HORIZONTAL_DOWN 1
cglobal pred8x8l_horizontal_down_%1, 4,5
    sub          r0, r3
    lea          r4, [r0+r3*2]
    movq        mm0, [r0+r3*1-8]
    punpckhbw   mm0, [r0+r3*0-8]
    movq        mm1, [r4+r3*1-8]
    punpckhbw   mm1, [r0+r3*2-8]
    mov          r4, r0
    punpckhwd   mm1, mm0
    lea          r0, [r0+r3*4]
    movq        mm2, [r0+r3*1-8]
    punpckhbw   mm2, [r0+r3*0-8]
    lea          r0, [r0+r3*2]
    movq        mm3, [r0+r3*1-8]
    punpckhbw   mm3, [r0+r3*0-8]
    punpckhwd   mm3, mm2
    punpckhdq   mm3, mm1
    lea          r0, [r0+r3*2]
    movq        mm0, [r0+r3*0-8]
    movq        mm1, [r4]
    mov          r0, r4
    movq        mm4, mm3
    movq        mm2, mm3
    PALIGNR     mm4, mm0, 7, mm0
    PALIGNR     mm1, mm2, 1, mm2
    test        r1, r1
    jnz .do_left
.fix_lt_1:
    movq        mm5, mm3
    pxor        mm5, mm4
    psrlq       mm5, 56
    psllq       mm5, 48
    pxor        mm1, mm5
    jmp .do_left
.fix_lt_2:
    movq        mm5, mm3
    pxor        mm5, mm2
    psllq       mm5, 56
    psrlq       mm5, 56
    pxor        mm2, mm5
    test         r2, r2
    jnz .do_top
.fix_tr_1:
    movq        mm5, mm3
    pxor        mm5, mm1
    psrlq       mm5, 56
    psllq       mm5, 56
    pxor        mm1, mm5
    jmp .do_top
.fix_tr_2:
    punpckhbw   mm3, mm3
    pshufw      mm1, mm3, 0xFF
    jmp .do_topright
.do_left:
    movq        mm0, mm4
    PRED4x4_LOWPASS mm2, mm1, mm4, mm3, mm5
    movq2dq    xmm0, mm2
    pslldq     xmm0, 8
    movq        mm4, mm0
    PRED4x4_LOWPASS mm1, mm3, mm0, mm4, mm5
    movq2dq    xmm2, mm1
    pslldq     xmm2, 15
    psrldq     xmm2, 8
    por        xmm0, xmm2
    movq        mm0, [r0-8]
    movq        mm3, [r0]
    movq        mm1, [r0+8]
    movq        mm2, mm3
    movq        mm4, mm3
    PALIGNR     mm2, mm0, 7, mm0
    PALIGNR     mm1, mm4, 1, mm4
    test         r1, r1
    jz .fix_lt_2
    test         r2, r2
    jz .fix_tr_1
.do_top:
    PRED4x4_LOWPASS mm4, mm2, mm1, mm3, mm5
    movq2dq    xmm1, mm4
    test         r2, r2
    jz .fix_tr_2
    movq        mm0, [r0+8]
    movq        mm5, mm0
    movq        mm2, mm0
    movq        mm4, mm0
    psrlq       mm5, 56
    PALIGNR     mm2, mm3, 7, mm3
    PALIGNR     mm5, mm4, 1, mm4
    PRED4x4_LOWPASS mm1, mm2, mm5, mm0, mm4
.do_topright:
    movq2dq    xmm5, mm1
    pslldq     xmm5, 8
    por        xmm1, xmm5
INIT_XMM
    lea         r2, [r4+r3*2]
    movdqa    xmm2, xmm1
    movdqa    xmm3, xmm1
    PALIGNR   xmm1, xmm0, 7, xmm4
    PALIGNR   xmm2, xmm0, 9, xmm5
    lea         r1, [r2+r3*2]
    PALIGNR   xmm3, xmm0, 8, xmm0
    movdqa    xmm4, xmm1
    pavgb     xmm4, xmm3
    lea         r0, [r1+r3*2]
    PRED4x4_LOWPASS xmm0, xmm1, xmm2, xmm3, xmm5
    punpcklbw xmm4, xmm0
    movhlps   xmm0, xmm4
    movq   [r0+r3*2], xmm4
    movq   [r2+r3*2], xmm0
    psrldq xmm4, 2
    psrldq xmm0, 2
    movq   [r0+r3*1], xmm4
    movq   [r2+r3*1], xmm0
    psrldq xmm4, 2
    psrldq xmm0, 2
    movq   [r1+r3*2], xmm4
    movq   [r4+r3*2], xmm0
    psrldq xmm4, 2
    psrldq xmm0, 2
    movq   [r1+r3*1], xmm4
    movq   [r4+r3*1], xmm0
    RET
%endmacro

INIT_MMX
%define PALIGNR PALIGNR_MMX
PRED8x8L_HORIZONTAL_DOWN sse2
INIT_MMX
%define PALIGNR PALIGNR_SSSE3
PRED8x8L_HORIZONTAL_DOWN ssse3

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

;-----------------------------------------------------------------------------
; void pred4x4_down_left_mmxext(uint8_t *src, const uint8_t *topright, int stride)
;-----------------------------------------------------------------------------
INIT_MMX
cglobal pred4x4_down_left_mmxext, 3,3
    sub       r0, r2
    movq      m1, [r0]
    punpckldq m1, [r1]
    movq      m2, m1
    movq      m3, m1
    psllq     m1, 8
    pxor      m2, m1
    psrlq     m2, 8
    pxor      m2, m3
    PRED4x4_LOWPASS m0, m1, m2, m3, m4
    lea       r1, [r0+r2*2]
    psrlq     m0, 8
    movd      [r0+r2*1], m0
    psrlq     m0, 8
    movd      [r0+r2*2], m0
    psrlq     m0, 8
    movd      [r1+r2*1], m0
    psrlq     m0, 8
    movd      [r1+r2*2], m0
    RET

;-----------------------------------------------------------------------------
; void pred4x4_vertical_left_mmxext(uint8_t *src, const uint8_t *topright, int stride)
;-----------------------------------------------------------------------------

INIT_MMX
cglobal pred4x4_vertical_left_mmxext, 3,3
    sub       r0, r2
    movq      m1, [r0]
    punpckldq m1, [r1]
    movq      m3, m1
    movq      m2, m1
    psrlq     m3, 8
    psrlq     m2, 16
    movq      m4, m3
    pavgb     m4, m1
    PRED4x4_LOWPASS m0, m1, m2, m3, m5
    lea       r1, [r0+r2*2]
    movh      [r0+r2*1], m4
    movh      [r0+r2*2], m0
    psrlq     m4, 8
    psrlq     m0, 8
    movh      [r1+r2*1], m4
    movh      [r1+r2*2], m0
    RET

;-----------------------------------------------------------------------------
; void pred4x4_horizontal_up_mmxext(uint8_t *src, const uint8_t *topright, int stride)
;-----------------------------------------------------------------------------

INIT_MMX
cglobal pred4x4_horizontal_up_mmxext, 3,3
    sub       r0, r2
    lea       r1, [r0+r2*2]
    movd      m0, [r0+r2*1-4]
    punpcklbw m0, [r0+r2*2-4]
    movd      m1, [r1+r2*1-4]
    punpcklbw m1, [r1+r2*2-4]
    punpckhwd m0, m1
    movq      m1, m0
    punpckhbw m1, m1
    pshufw    m1, m1, 0xFF
    punpckhdq m0, m1
    movq      m2, m0
    movq      m3, m0
    movq      m7, m0
    psrlq     m2, 16
    psrlq     m3, 8
    pavgb     m7, m3
    PRED4x4_LOWPASS m4, m0, m2, m3, m5
    punpcklbw m7, m4
    movd    [r0+r2*1], m7
    psrlq    m7, 16
    movd    [r0+r2*2], m7
    psrlq    m7, 16
    movd    [r1+r2*1], m7
    movd    [r1+r2*2], m1
    RET

;-----------------------------------------------------------------------------
; void pred4x4_horizontal_down_mmxext(uint8_t *src, const uint8_t *topright, int stride)
;-----------------------------------------------------------------------------

INIT_MMX
%define PALIGNR PALIGNR_MMX
cglobal pred4x4_horizontal_down_mmxext, 3,3
    sub       r0, r2
    lea       r1, [r0+r2*2]
    movh      m0, [r0-4]      ; lt ..
    punpckldq m0, [r0]        ; t3 t2 t1 t0 lt .. .. ..
    psllq     m0, 8           ; t2 t1 t0 lt .. .. .. ..
    movd      m1, [r1+r2*2-4] ; l3
    punpcklbw m1, [r1+r2*1-4] ; l2 l3
    movd      m2, [r0+r2*2-4] ; l1
    punpcklbw m2, [r0+r2*1-4] ; l0 l1
    punpckhwd m1, m2          ; l0 l1 l2 l3
    punpckhdq m1, m0          ; t2 t1 t0 lt l0 l1 l2 l3
    movq      m0, m1
    movq      m2, m1
    movq      m5, m1
    psrlq     m0, 16          ; .. .. t2 t1 t0 lt l0 l1
    psrlq     m2, 8           ; .. t2 t1 t0 lt l0 l1 l2
    pavgb     m5, m2
    PRED4x4_LOWPASS m3, m1, m0, m2, m4
    punpcklbw m5, m3
    psrlq     m3, 32
    PALIGNR   m3, m5, 6, m4
    movh      [r1+r2*2], m5
    psrlq     m5, 16
    movh      [r1+r2*1], m5
    psrlq     m5, 16
    movh      [r0+r2*2], m5
    movh      [r0+r2*1], m3
    RET

;-----------------------------------------------------------------------------
; void pred4x4_vertical_right_mmxext(uint8_t *src, const uint8_t *topright, int stride)
;-----------------------------------------------------------------------------

INIT_MMX
%define PALIGNR PALIGNR_MMX
cglobal pred4x4_vertical_right_mmxext, 3,3
    sub     r0, r2
    lea     r1, [r0+r2*2]
    movh    m0, [r0]                    ; ........t3t2t1t0
    movq    m5, m0
    PALIGNR m0, [r0-8], 7, m1           ; ......t3t2t1t0lt
    pavgb   m5, m0
    PALIGNR m0, [r0+r2*1-8], 7, m1      ; ....t3t2t1t0ltl0
    movq    m1, m0
    PALIGNR m0, [r0+r2*2-8], 7, m2      ; ..t3t2t1t0ltl0l1
    movq    m2, m0
    PALIGNR m0, [r1+r2*1-8], 7, m3      ; t3t2t1t0ltl0l1l2
    PRED4x4_LOWPASS m3, m1, m0, m2, m4
    movq    m1, m3
    psrlq   m3, 16
    psllq   m1, 48
    movh    [r0+r2*1], m5
    movh    [r0+r2*2], m3
    PALIGNR m5, m1, 7, m2
    psllq   m1, 8
    movh    [r1+r2*1], m5
    PALIGNR m3, m1, 7, m1
    movh    [r1+r2*2], m3
    RET

;-----------------------------------------------------------------------------
; void pred4x4_down_right_mmxext(uint8_t *src, const uint8_t *topright, int stride)
;-----------------------------------------------------------------------------

INIT_MMX
%define PALIGNR PALIGNR_MMX
cglobal pred4x4_down_right_mmxext, 3,3
    sub       r0, r2
    lea       r1, [r0+r2*2]
    movq      m1, [r1-8]
    movq      m2, [r0+r2*1-8]
    punpckhbw m2, [r0-8]
    movh      m3, [r0]
    punpckhwd m1, m2
    PALIGNR   m3, m1, 5, m1
    movq      m1, m3
    PALIGNR   m3, [r1+r2*1-8], 7, m4
    movq      m2, m3
    PALIGNR   m3, [r1+r2*2-8], 7, m4
    PRED4x4_LOWPASS m0, m3, m1, m2, m4
    movh      [r1+r2*2], m0
    psrlq     m0, 8
    movh      [r1+r2*1], m0
    psrlq     m0, 8
    movh      [r0+r2*2], m0
    psrlq     m0, 8
    movh      [r0+r2*1], m0
    RET
