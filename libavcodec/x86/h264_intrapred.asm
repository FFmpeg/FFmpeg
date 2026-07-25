;******************************************************************************
;* H.264 intra prediction asm optimizations
;* Copyright (c) 2010 Fiona Glaser
;* Copyright (c) 2010 Holger Lubitz
;* Copyright (c) 2010 Loren Merritt
;* Copyright (c) 2010 Ronald S. Bultje
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
cextern pw_8

;-----------------------------------------------------------------------------
; void ff_pred16x16_vertical_8(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse
cglobal pred16x16_vertical_8, 2,3
    sub   r0, r1
    mov   r2, 4
    movaps   m0, [r0]
.loop:
    movaps [r0+r1*1], m0
    movaps [r0+r1*2], m0
    lea   r0, [r0+r1*2]
    movaps [r0+r1*1], m0
    movaps [r0+r1*2], m0
    lea   r0, [r0+r1*2]
    dec   r2
    jg .loop
    RET

;-----------------------------------------------------------------------------
; void ff_pred16x16_horizontal_8(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------

%macro PRED16x16_H 0
cglobal pred16x16_horizontal_8, 2,3
    mov       r2, 8
%if cpuflag(ssse3) && notcpuflag(avx2)
    mova      m2, [pb_3]
%endif
.loop:
%if cpuflag(avx2)
    vpbroadcastb m0, [r0+r1*0-1]
    vpbroadcastb m1, [r0+r1*1-1]
%else
    movd      m0, [r0+r1*0-4]
    movd      m1, [r0+r1*1-4]

%if cpuflag(ssse3)
    pshufb    m0, m2
    pshufb    m1, m2
%else
    punpcklbw m0, m0
    punpcklbw m1, m1
    SPLATW    m0, m0, 3
    SPLATW    m1, m1, 3
%endif
%endif

    mova [r0+r1*0], m0
    mova [r0+r1*1], m1
    lea       r0, [r0+r1*2]
    dec       r2
    jg .loop
    RET
%endmacro

INIT_XMM sse2
PRED16x16_H
INIT_XMM ssse3
PRED16x16_H
INIT_XMM avx2
PRED16x16_H

;-----------------------------------------------------------------------------
; void ff_pred16x16_dc_8(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------

%macro PRED16x16_DC 0
cglobal pred16x16_dc_8, 2,7,2
    mov       r4, r0
    sub       r0, r1
    pxor      m0, m0
    psadbw    m0, [r0]
    psrldq    m1, m0, 8
    dec        r0
    movzx     r5d, byte [r0+r1*1]
    paddw      m0, m1
    movd      r6d, m0
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
%if cpuflag(ssse3)
    pxor       m1, m1
%endif
    SPLATB_REG m0, r2, m1

    mov       r3d, 4
.loop:
    mova [r4+r1*0], m0
    mova [r4+r1*1], m0
    lea   r4, [r4+r1*2]
    mova [r4+r1*0], m0
    mova [r4+r1*1], m0
    lea   r4, [r4+r1*2]
    dec   r3d
    jg .loop
    RET
%endmacro

INIT_XMM sse2
PRED16x16_DC
INIT_XMM ssse3
PRED16x16_DC

;-----------------------------------------------------------------------------
; void ff_pred16x16_tm_vp8_8(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred16x16_tm_vp8_8, 2,6,6
    sub          r0, r1
    pxor         m2, m2
    mova         m0, [r0]
    mova         m1, m0
    punpcklbw    m0, m2
    punpckhbw    m1, m2
    movzx       r4d, byte [r0-1]
    mov         r5d, 8
.loop:
    movzx       r2d, byte [r0+r1*1-1]
    movzx       r3d, byte [r0+r1*2-1]
    sub         r2d, r4d
    sub         r3d, r4d
    movd         m2, r2d
    movd         m4, r3d
    pshuflw      m2, m2, 0
    pshuflw      m4, m4, 0
    punpcklqdq   m2, m2
    punpcklqdq   m4, m4
    mova         m3, m2
    mova         m5, m4
    paddw        m2, m0
    paddw        m3, m1
    paddw        m4, m0
    paddw        m5, m1
    packuswb     m2, m3
    packuswb     m4, m5
    mova  [r0+r1*1], m2
    mova  [r0+r1*2], m4
    lea          r0, [r0+r1*2]
    dec         r5d
    jg .loop
    RET

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
cglobal pred16x16_tm_vp8_8, 2, 4, 5, dst, stride, stride3, iteration
    sub                       dstq, strideq
    pmovzxbw                    m0, [dstq]
    vpbroadcastb               xm1, [r0-1]
    pmovzxbw                    m1, xm1
    psubw                       m0, m1
    mov                 iterationd, 4
    lea                   stride3q, [strideq*3]
.loop:
    vpbroadcastb               xm1, [dstq+strideq*1-1]
    vpbroadcastb               xm2, [dstq+strideq*2-1]
    vpbroadcastb               xm3, [dstq+stride3q-1]
    vpbroadcastb               xm4, [dstq+strideq*4-1]
    pmovzxbw                    m1, xm1
    pmovzxbw                    m2, xm2
    pmovzxbw                    m3, xm3
    pmovzxbw                    m4, xm4
    paddw                       m1, m0
    paddw                       m2, m0
    paddw                       m3, m0
    paddw                       m4, m0
    vpackuswb                   m1, m1, m2
    vpackuswb                   m3, m3, m4
    vpermq                      m1, m1, q3120
    vpermq                      m3, m3, q3120
    movdqa        [dstq+strideq*1], xm1
    vextracti128  [dstq+strideq*2], m1, 1
    movdqa       [dstq+stride3q*1], xm3
    vextracti128  [dstq+strideq*4], m3, 1
    lea                       dstq, [dstq+strideq*4]
    dec                 iterationd
    jg .loop
    RET
%endif

;-----------------------------------------------------------------------------
; void ff_pred16x16_plane_*_8(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------

%macro H264_PRED16x16_PLANE 1
cglobal pred16x16_plane_%1_8, 2,9,5
    mov          r2, r1           ; +stride
    neg          r1               ; -stride

    movh         m0, [r0+r1  -1]
%if cpuflag(ssse3)
    movhps       m0, [r0+r1  +8]
    pmaddubsw    m0, [plane_shuf] ; H coefficients
%else ; sse2
    pxor         m2, m2
    movh         m1, [r0+r1  +8]
    punpcklbw    m0, m2
    punpcklbw    m1, m2
    pmullw       m0, [pw_m8tom1]
    pmullw       m1, [pw_1to8]
    paddw        m0, m1
%endif
    movhlps      m1, m0
    paddw        m0, m1
    PSHUFLW      m1, m0, q0032
    paddw        m0, m1
    PSHUFLW      m1, m0, q0001
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

%ifidn %1, h264
    lea          r5, [r5*5+32]
    sar          r5, 6
%elifidn %1, rv40
    lea          r5, [r5*5]
    sar          r5, 6
%elifidn %1, svq3
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
%ifnidn %1, svq3
%ifidn %1, h264
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
    SPLATW       m0, m0, 0        ; H
    SPLATW       m1, m1, 0        ; V
    SPLATW       m3, m3, 0        ; a
%ifidn %1, svq3
    SWAP          0, 1
%endif
    mova         m2, m0
    pmullw       m0, [pw_0to7]    ; 0*H, 1*H, ..., 7*H  (words)
    psllw        m2, 3
    paddw        m0, m3           ; a + {0,1,2,3,4,5,6,7}*H
    paddw        m2, m0           ; a + {8,9,10,11,12,13,14,15}*H

    mov          r4, 8
.loop:
    mova         m3, m0           ; b[0..7]
    mova         m4, m2           ; b[8..15]
    psraw        m3, 5
    psraw        m4, 5
    packuswb     m3, m4
    mova       [r0], m3
    paddw        m0, m1
    paddw        m2, m1

    mova         m3, m0           ; b[0..7]
    mova         m4, m2           ; b[8..15]
    psraw        m3, 5
    psraw        m4, 5
    packuswb     m3, m4
    mova    [r0+r2], m3
    paddw        m0, m1
    paddw        m2, m1

    lea          r0, [r0+r2*2]
    dec          r4
    jg .loop
    RET
%endmacro

INIT_XMM sse2
H264_PRED16x16_PLANE h264
H264_PRED16x16_PLANE rv40
H264_PRED16x16_PLANE svq3
INIT_XMM ssse3
H264_PRED16x16_PLANE h264
H264_PRED16x16_PLANE rv40
H264_PRED16x16_PLANE svq3

;-----------------------------------------------------------------------------
; void ff_pred8x8_plane_8(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------

%macro H264_PRED8x8_PLANE 0
cglobal pred8x8_plane_8, 2,9,5
    mov          r2, r1           ; +stride
    neg          r1               ; -stride

    movd         m0, [r0+r1  -1]
%if cpuflag(ssse3)
    movhps       m0, [r0+r1  +4]   ; this reads 4 bytes more than necessary
    pmaddubsw    m0, [plane8_shuf] ; H coefficients
%else ; sse2
    pxor         m2, m2
    movd         m1, [r0+r1  +4]
    punpckldq    m0, m1
    punpcklbw    m0, m2
    pmullw       m0, [pw_m4to4]
%endif
    movhlps      m1, m0
    paddw        m0, m1

%if notcpuflag(ssse3)
    PSHUFLW      m1, m0, q0032
    paddw        m0, m1
%endif ; !ssse3

    PSHUFLW      m1, m0, q0001
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
    SPLATW       m0, m0, 0        ; H
    SPLATW       m1, m1, 0        ; V
    SPLATW       m3, m3, 0        ; a
    pmullw       m0, [pw_0to7]    ; 0*H, 1*H, ..., 7*H  (words)
    paddw        m0, m3           ; a + {0,1,2,3,4,5,6,7}*H

    mov          r4, 4
ALIGN 16
.loop:
    mova         m3, m0           ; b[0..7]
    paddw        m0, m1
    psraw        m3, 5
    mova         m4, m0           ; V+b[0..7]
    paddw        m0, m1
    psraw        m4, 5
    packuswb     m3, m4
    movh       [r0], m3
    movhps  [r0+r2], m3

    lea          r0, [r0+r2*2]
    dec          r4
    jg .loop
    RET
%endmacro

INIT_XMM sse2
H264_PRED8x8_PLANE
INIT_XMM ssse3
H264_PRED8x8_PLANE

;-----------------------------------------------------------------------------
; void ff_pred8x8_vertical_8(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8_vertical_8, 2,2
    sub    r0, r1
    movq   m0, [r0]
%rep 3
    movq [r0+r1*1], m0
    movq [r0+r1*2], m0
    lea    r0, [r0+r1*2]
%endrep
    movq [r0+r1*1], m0
    movq [r0+r1*2], m0
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8_horizontal_8(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------

%macro PRED8x8_H 0
cglobal pred8x8_horizontal_8, 2,3,3
    mov       r2, 4
%if cpuflag(ssse3) && notcpuflag(avx2)
    mova      m2, [pb_3]
%endif
.loop:
%if cpuflag(avx2)
    vpbroadcastb m0, [r0+r1*0-1]
    vpbroadcastb m1, [r0+r1*1-1]
%else
    SPLATB_LOAD m0, r0+r1*0-1, m2
    SPLATB_LOAD m1, r0+r1*1-1, m2
%endif
    movq [r0+r1*0], m0
    movq [r0+r1*1], m1
    lea       r0, [r0+r1*2]
    dec       r2
    jg .loop
    RET
%endmacro

INIT_XMM sse2
PRED8x8_H
INIT_XMM ssse3
PRED8x8_H
INIT_XMM avx2
PRED8x8_H

;-----------------------------------------------------------------------------
; void ff_pred8x8_top_dc_8_sse2(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------
INIT_XMM sse2
cglobal pred8x8_top_dc_8, 2,5,2
    sub         r0, r1
    movq        m0, [r0]
    pxor        m1, m1
    lea         r2, [r0+r1*2]
    punpcklbw   m0, m1
    psadbw      m0, m1       ; s0,0,0,0,s1,0,0,0 (w)
    lea         r3, [r2+r1*2]
    psrlw       m0, 1
    pavgw       m0, m1
    pshuflw     m0, m0, 0    ; dc0,dc0,dc0,dc0,dc1,0,0,0
    pshufhw     m0, m0, 0    ; dc0,dc1 (w)
    packuswb    m0, m1       ; dc0,dc1 (b)
    movq [r0+r1*1], m0
    movq [r0+r1*2], m0
    lea         r0, [r3+r1*2]
    movq [r2+r1*1], m0
    movq [r2+r1*2], m0
    movq [r3+r1*1], m0
    movq [r3+r1*2], m0
    movq [r0+r1*1], m0
    movq [r0+r1*2], m0
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8_dc_8_sse2(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8_dc_8, 2,5,5
    sub       r0, r1
    pxor      m4, m4
    movd      m0, [r0+0]
    movd      m1, [r0+4]
    psadbw    m0, m4            ; s0
    mov       r4, r0
    psadbw    m1, m4            ; s1

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
    pshuflw   m3, m0, q3312     ; s2, s1, s3, s3
    lea       r2, [r0+r1*2]
    pshuflw   m0, m0, q1310     ; s0, s1, s3, s1
    paddw     m0, m3
    lea       r3, [r2+r1*2]
    psrlw     m0, 2
    pavgw     m0, m4            ; s0+s2, s1, s3, s1+s3
    lea       r4, [r3+r1*2]
    packuswb  m0, m0
    punpcklbw m0, m0
    punpcklwd m0, m0
    movq   [r0+r1*1], m0
    movq   [r0+r1*2], m0
    movq   [r2+r1*1], m0
    movq   [r2+r1*2], m0
    movhps [r3+r1*1], m0
    movhps [r3+r1*2], m0
    movhps [r4+r1*1], m0
    movhps [r4+r1*2], m0
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8_dc_rv40_8(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8_dc_rv40_8, 2,7,2
    mov       r4, r0
    sub       r0, r1
    movq      m1, [r0]
    pxor      m0, m0
    psadbw    m0, m1
    dec        r0
    movzx     r5d, byte [r0+r1*1]
    movd      r6d, m0
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
    movd       m0, r2d
    punpcklbw  m0, m0
    pshuflw    m0, m0, 0
    mov       r3d, 4
.loop:
    movq [r4+r1*0], m0
    movq [r4+r1*1], m0
    lea   r4, [r4+r1*2]
    dec   r3d
    jg .loop
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8_tm_vp8_8(uint8_t *src, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8_tm_vp8_8, 2,6,4
    sub          r0, r1
    pxor         m1, m1
    movq         m0, [r0]
    punpcklbw    m0, m1
    movzx       r4d, byte [r0-1]
    mov         r5d, 4
.loop:
    movzx       r2d, byte [r0+r1*1-1]
    movzx       r3d, byte [r0+r1*2-1]
    sub         r2d, r4d
    sub         r3d, r4d
    movd         m2, r2d
    movd         m3, r3d
    pshuflw      m2, m2, 0
    pshuflw      m3, m3, 0
    punpcklqdq   m2, m2
    punpcklqdq   m3, m3
    paddw        m2, m0
    paddw        m3, m0
    packuswb     m2, m3
    movq   [r0+r1*1], m2
    movhps [r0+r1*2], m2
    lea          r0, [r0+r1*2]
    dec         r5d
    jg .loop
    RET

INIT_XMM ssse3
cglobal pred8x8_tm_vp8_8, 2,3,6
    sub          r0, r1
    mova         m4, [tm_shuf]
    pxor         m1, m1
    movq         m0, [r0]
    punpcklbw    m0, m1
    movd         m5, [r0-4]
    pshufb       m5, m4
    mov         r2d, 4
.loop:
    movd          m2, [r0+r1*1-4]
    movd          m3, [r0+r1*2-4]
    pshufb        m2, m4
    pshufb        m3, m4
    psubw         m2, m5
    psubw         m3, m5
    paddw         m2, m0
    paddw         m3, m0
    packuswb      m2, m3
    movq   [r0+r1*1], m2
    movhps [r0+r1*2], m2
    lea          r0, [r0+r1*2]
    dec         r2d
    jg .loop
    RET

; dest, left, right, src, tmp
; output: %1 = (t[n-1] + t[n]*2 + t[n+1] + 2) >> 2
%macro PRED4x4_LOWPASS 5
    mova    %5, %2
    pavgb   %2, %3
    pxor    %3, %5
%ifnidn %1, %4
    mova    %1, %4
%endif
    pand    %3, [pb_1]
    psubusb %2, %3
    pavgb   %1, %2
%endmacro

;-----------------------------------------------------------------------------
; void ff_pred8x8l_top_dc_8(uint8_t *src, int has_topleft, int has_topright,
;                           ptrdiff_t stride)
;-----------------------------------------------------------------------------
INIT_XMM sse2
cglobal pred8x8l_top_dc_8, 4,4,6
    sub          r0, r3
    movu         m2, [r0-8]
    movu         m3, [r0]
    mova         m1, m3
    psrldq       m2, 7
    psrldq       m1, 1
    test        r1d, r1d ; top_left
    jnz .has_topleft
    pxor         m5, m3, m2
    psllq        m5, 56
    psrlq        m5, 56
    pxor         m2, m5
.has_topleft:
    test        r2d, r2d ; top_right
    jnz .has_topright
    pxor         m5, m3, m1
    psrlq        m5, 56
    psllq        m5, 56
    pxor         m1, m5
.has_topright:
    pxor     m4, m4
    PRED4x4_LOWPASS m3, m2, m1, m3, m5
    psadbw   m4, m3
    paddw    m4, [pw_4]
    psrlw    m4, 3
    punpcklbw m4, m4
    pshuflw  m4, m4, 0
%rep 3
    movq [r0+r3*1], m4
    movq [r0+r3*2], m4
    lea    r0, [r0+r3*2]
%endrep
    movq [r0+r3*1], m4
    movq [r0+r3*2], m4
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8l_dc_8(uint8_t *src, int has_topleft, int has_topright,
;                       ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8l_dc_8, 4,5,6
    sub          r0, r3
    lea          r4, [r0+r3*2]
    movd         m0, [r0+r3*1-4]
    movd         m4, [r0+r3*0-4]
    punpcklbw    m0, m4
    movd         m1, [r4+r3*1-4]
    movd         m4, [r0+r3*2-4]
    punpcklbw    m1, m4
    mov          r4, r0
    punpcklwd    m1, m0
    lea          r0, [r0+r3*4]
    movd         m2, [r0+r3*1-4]
    movd         m4, [r0+r3*0-4]
    punpcklbw    m2, m4
    lea          r0, [r0+r3*2]
    movd         m3, [r0+r3*1-4]
    movd         m4, [r0+r3*0-4]
    punpcklbw    m3, m4
    punpcklwd    m3, m2
    shufps       m3, m1, q3231
    pshufd       m3, m3, q0031
    lea          r0, [r0+r3*2]
    movq         m0, [r0+r3*0-8]
    movq         m1, [r4]
    mov          r0, r4
    mova         m4, m3
    mova         m2, m3
    punpcklqdq   m0, m4
    psrldq       m0, 7
    punpcklqdq   m2, m1
    psrldq       m2, 1
    test        r1d, r1d
    jnz .do_left
    pxor         m5, m3, m0
    psrlq        m5, 56
    psllq        m5, 48
    pxor         m2, m5
.do_left:
    mova         m4, m0
    PRED4x4_LOWPASS m1, m2, m4, m3, m5
    mova         m4, m0
    PRED4x4_LOWPASS m2, m3, m0, m4, m5
    psllq        m2, 56
    punpcklqdq   m2, m1
    psrldq       m2, 7
    movu         m0, [r0-8]
    movu         m3, [r0]
    mova         m4, m3
    psrldq       m0, 7
    psrldq       m4, 1
    test        r1d, r1d
    jnz .skip_fix_lt_2
    pxor         m1, m3, m0
    psllq        m1, 56
    psrlq        m1, 56
    pxor         m0, m1
.skip_fix_lt_2:
    test        r2d, r2d
    jnz .body
    pxor         m1, m3, m4
    psrlq        m1, 56
    psllq        m1, 56
    pxor         m4, m1
.body:
    lea          r1, [r0+r3*2]
    PRED4x4_LOWPASS m3, m0, m4, m3, m1
    pxor         m1, m1
    lea          r2, [r1+r3*2]
    psadbw       m2, m1
    psadbw       m3, m1
    paddw        m3, [pw_8]
    paddw        m3, m2
    lea          r4, [r2+r3*2]
    psrlw        m3, 4
    punpcklbw    m3, m3
    pshuflw      m3, m3, 0
    movq  [r0+r3*1], m3
    movq  [r0+r3*2], m3
    movq  [r1+r3*1], m3
    movq  [r1+r3*2], m3
    movq  [r2+r3*1], m3
    movq  [r2+r3*2], m3
    movq  [r4+r3*1], m3
    movq  [r4+r3*2], m3
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8l_horizontal_8(uint8_t *src, int has_topleft,
;                               int has_topright, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8l_horizontal_8, 4,4,6
    sub          r0, r3
    lea          r2, [r0+r3*2]
    movd         m0, [r0+r3*1-4]
    test        r1d, r1d
    lea          r1, [r0+r3]
    cmovnz       r1, r0
    movd         m4, [r1+r3*0-4]
    punpcklbw    m0, m4
    movd         m1, [r2+r3*1-4]
    movd         m4, [r0+r3*2-4]
    punpcklbw    m1, m4
    mov          r2, r0
    punpcklwd    m1, m0
    lea          r0, [r0+r3*4]
    movd         m2, [r0+r3*1-4]
    movd         m4, [r0+r3*0-4]
    punpcklbw    m2, m4
    lea          r0, [r0+r3*2]
    movd         m3, [r0+r3*1-4]
    movd         m4, [r0+r3*0-4]
    punpcklbw    m3, m4
    punpcklwd    m3, m2
    punpckhdq    m3, m1
    pshufd       m3, m3, q3232
    lea          r0, [r0+r3*2]
    movq         m0, [r0+r3*0-8]
    movq         m1, [r1+r3*0-8]
    mov          r0, r2
    mova         m2, m3
    punpcklqdq   m0, m3
    psrldq       m0, 7
    mova         m4, m0
    punpcklqdq   m2, m1
    psrldq       m2, 1
    PRED4x4_LOWPASS m1, m2, m4, m3, m5
    mova        m4, m0
    PRED4x4_LOWPASS m2, m3, m0, m4, m5
    psllq       m2, 56
    punpcklqdq  m2, m1
    psrldq      m2, 7
    lea         r1, [r0+r3*2]
    punpcklbw   m2, m2
    pshufhw     m0, m2, q3333
    pshufhw     m1, m2, q2222
    lea         r2, [r1+r3*2]
    pshufhw     m3, m2, q1111
    pshufhw     m4, m2, q0000
    movhps   [r0+r3*1], m0
    movhps   [r0+r3*2], m1
    movhps   [r1+r3*1], m3
    movhps   [r1+r3*2], m4
    pshuflw     m0, m2, q3333
    pshuflw     m1, m2, q2222
    pshuflw     m3, m2, q1111
    pshuflw     m4, m2, q0000
    movq [r2+r3*1], m0
    movq [r2+r3*2], m1
    lea         r0, [r2+r3*2]
    movq [r0+r3*1], m3
    movq [r0+r3*2], m4
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8l_vertical_8(uint8_t *src, int has_topleft, int has_topright,
;                             ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8l_vertical_8, 4,4,5
    sub          r0, r3
    movu         m2, [r0-8]
    movu         m3, [r0]
    mova         m1, m3
    psrldq       m2, 7
    psrldq       m1, 1
    test        r1d, r1d ; top_left
    jnz .check_tr
    pxor         m4, m3, m2
    psllq        m4, 56
    psrlq        m4, 56
    pxor         m2, m4
.check_tr:
    test        r2d, r2d ; top_right
    jnz .body
    pxor         m4, m3, m1
    psrlq        m4, 56
    psllq        m4, 56
    pxor         m1, m4
.body:
    PRED4x4_LOWPASS m3, m2, m1, m3, m4
%rep 3
    movq  [r0+r3*1], m3
    movq  [r0+r3*2], m3
    lea    r0, [r0+r3*2]
%endrep
    movq  [r0+r3*1], m3
    movq  [r0+r3*2], m3
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8l_down_left_8(uint8_t *src, int has_topleft,
;                              int has_topright, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8l_down_left_8, 4,4,6
    sub          r0, r3
    movu         m2, [r0-8]
    movu         m1, [r0]
    mova         m3, m1
    psrldq       m2, 7
    psrldq       m1, 1
    test        r1d, r1d ; top_left
    jnz .check_tr
.fix_lt:
    pxor         m5, m3, m2
    psllq        m5, 56
    psrlq        m5, 56
    pxor         m2, m5
.check_tr:
    test        r2d, r2d ; top_right
    jnz .do_top
.fix_tr_1:
    pxor         m5, m3, m1
    psrlq        m5, 56
    psllq        m5, 56
    pxor         m1, m5
.do_top:
    PRED4x4_LOWPASS  m4, m2, m1, m3, m5
    movq         m2, m4
    test        r2d, r2d ; top_right
    jnz .has_top_right
.fix_tr_2:
    punpcklbw    m3, m3
    pshufhw      m1, m3, q3333
    pshufd       m1, m1, q3232
    jmp .do_topright
.has_top_right:
    movq         m0, [r0+8]
    psrlq        m5, m0, 56
    punpcklqdq   m3, m0
    psrldq       m3, 7
    punpcklqdq   m4, m0, m5
    psrldq       m4, 1
    PRED4x4_LOWPASS m1, m3, m4, m0, m5
.do_topright:
    mova        m4, m1
    psrlq       m1, 56
    pslldq      m1, 15
    lea         r1, [r0+r3*2]
    pslldq      m4, 8
    por         m2, m4
    mova        m3, m2
    psrldq      m2, 1
    por         m2, m1
    lea         r2, [r1+r3*2]
    pslldq      m1, m3, 1
    PRED4x4_LOWPASS m3, m1, m2, m3, m4
    psrldq      m3, 1
    movq [r0+r3*1], m3
    psrldq      m3, 1
    movq [r0+r3*2], m3
    psrldq      m3, 1
    lea         r0, [r2+r3*2]
    movq [r1+r3*1], m3
    psrldq      m3, 1
    movq [r1+r3*2], m3
    psrldq      m3, 1
    movq [r2+r3*1], m3
    psrldq      m3, 1
    movq [r2+r3*2], m3
    psrldq      m3, 1
    movq [r0+r3*1], m3
    psrldq      m3, 1
    movq [r0+r3*2], m3
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8l_down_right_8(uint8_t *src, int has_topleft,
;                               int has_topright, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8l_down_right_8, 4,5,7
    sub          r0, r3
    lea          r4, [r0+r3*2]
    movd         m0, [r0+r3*1-4]
    movd         m4, [r0+r3*0-4]
    punpcklbw    m0, m4
    movd         m1, [r4+r3*1-4]
    movd         m4, [r0+r3*2-4]
    punpcklbw    m1, m4
    mov          r4, r0
    punpcklwd    m1, m0
    lea          r0, [r0+r3*4]
    movd         m2, [r0+r3*1-4]
    movd         m4, [r0+r3*0-4]
    punpcklbw    m2, m4
    lea          r0, [r0+r3*2]
    movd         m3, [r0+r3*1-4]
    movd         m4, [r0+r3*0-4]
    punpcklbw    m3, m4
    punpcklwd    m3, m2
    punpckhdq    m3, m1
    pshufd       m3, m3, q3232
    lea          r0, [r0+r3*2]
    movq         m0, [r0+r3*0-8]
    movq         m1, [r4]
    mov          r0, r4
    mova         m2, m3
    punpcklqdq   m0, m3
    psrldq       m4, m0, 7
    punpcklqdq   m2, m1
    psrldq       m1, m2, 1
    test        r1d, r1d
    jnz .do_left
.fix_lt_1:
    pxor         m5, m3, m4
    psrlq        m5, 56
    psllq        m5, 48
    pxor         m1, m5
.do_left:
    mova         m0, m4
    PRED4x4_LOWPASS  m2, m1, m4, m3, m5
    mova         m4, m0
    movq         m6, m2
    PRED4x4_LOWPASS  m1, m3, m0, m4, m5
    psllq        m1, 56
    punpcklqdq   m1, m2
    psrldq       m1, 7
    mova         m0, m1
    movu         m2, [r0-8]
    movu         m1, [r0]
    mova         m3, m1
    psrldq       m2, 7
    psrldq       m1, 1
    jnz .check_tr
.fix_lt_2:
    pxor         m5, m3, m2
    psllq        m5, 56
    psrlq        m5, 56
    pxor         m2, m5
.check_tr:
    test        r2d, r2d
    jnz .do_top
.fix_tr_1:
    pxor         m5, m3, m1
    psrlq        m5, 56
    psllq        m5, 56
    pxor         m1, m5
.do_top:
    PRED4x4_LOWPASS  m4, m2, m1, m3, m5
    lea         r1, [r0+r3*2]
    mova        m1, m6
    pslldq      m4, 8
    por         m6, m4
    lea         r2, [r1+r3*2]
    pslldq      m4, 1
    por         m0, m4
    psrldq      m1, 7
    pslldq      m1, 15
    psrldq      m1, 7
    por         m0, m1
    lea         r0, [r2+r3*2]
    psrldq      m2, m6, 1
    PRED4x4_LOWPASS m3, m0, m2, m6, m4
    psrldq      m1, m3, 1
    movq [r0+r3*2], m3
    movq [r0+r3*1], m1
    psrldq      m3, 2
    psrldq      m1, 2
    movq [r2+r3*2], m3
    movq [r2+r3*1], m1
    psrldq      m3, 2
    psrldq      m1, 2
    movq [r1+r3*2], m3
    movq [r1+r3*1], m1
    psrldq      m3, 2
    psrldq      m1, 2
    movq [r4+r3*2], m3
    movq [r4+r3*1], m1
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8l_vertical_right_8(uint8_t *src, int has_topleft,
;                                   int has_topright, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8l_vertical_right_8, 4,5,6
    sub          r0, r3
    lea          r4, [r0+r3*2]
    movd         m0, [r0+r3*1-4]
    movd         m2, [r0+r3*0-4]
    punpcklbw    m0, m2
    movd         m1, [r4+r3*1-4]
    movd         m3, [r0+r3*2-4]
    punpcklbw    m1, m3
    mov          r4, r0
    punpcklwd    m1, m0
    lea          r0, [r0+r3*4]
    movd         m2, [r0+r3*1-4]
    movd         m5, [r0+r3*0-4]
    punpcklbw    m2, m5
    lea          r0, [r0+r3*2]
    movd         m3, [r0+r3*1-4]
    movd         m5, [r0+r3*0-4]
    punpcklbw    m3, m5
    punpcklwd    m3, m2
    punpckhdq    m3, m1
    pshufd       m3, m3, q3232
    lea          r0, [r0+r3*2]
    movq         m0, [r0+r3*0-8]
    movq         m1, [r4]
    mov          r0, r4
    punpcklqdq   m0, m3
    psrldq       m0, 7
    mova         m4, m0
    punpcklqdq   m2, m3, m1
    psrldq       m2, 1
    mova         m1, m2
    test        r1d, r1d
    jnz .do_left
.fix_lt_1:
    pxor         m5, m3, m4
    psrlq        m5, 56
    psllq        m5, 48
    pxor         m1, m5
.do_left:
    PRED4x4_LOWPASS  m2, m1, m4, m3, m5
    movq         m0, m2
    movu         m2, [r0-8]
    movu         m1, [r0]
    mova         m3, m1
    psrldq       m2, 7
    psrldq       m1, 1
    jnz .check_tr
.fix_lt_2:
    pxor         m5, m3, m2
    psllq        m5, 56
    psrlq        m5, 56
    pxor         m2, m5
.check_tr:
    test        r2d, r2d
    jnz .do_top
.fix_tr_1:
    pxor         m5, m3, m1
    psrlq        m5, 56
    psllq        m5, 56
    pxor         m1, m5
.do_top:
    PRED4x4_LOWPASS   m4, m2, m1, m3, m5
    lea           r1, [r0+r3*2]
    pslldq        m4, 8
    por           m0, m4
    mova          m1, m0
    lea           r2, [r0+r3*4]
    mova          m2, m0
    mova          m3, m0
    pslldq        m0, 1
    pslldq        m1, 2
    pavgb         m2, m0
    PRED4x4_LOWPASS   m0, m3, m1, m0, m5
    pandn         m4, [pw_ff00], m0
    mova          m5, m0
    psrlw         m0, 8
    packuswb      m4, m0
    movhlps       m0, m4
    movhps [r0+r3*2], m5
    movhps [r0+r3*1], m2
    psrldq        m5, 4
    movss         m5, m4
    psrldq        m2, 4
    movss         m2, m0
    lea           r0, [r2+r3*2]
    psrldq        m5, 1
    psrldq        m2, 1
    movq   [r0+r3*2], m5
    movq   [r0+r3*1], m2
    psrldq        m5, 1
    psrldq        m2, 1
    movq   [r2+r3*2], m5
    movq   [r2+r3*1], m2
    psrldq        m5, 1
    psrldq        m2, 1
    movq   [r1+r3*2], m5
    movq   [r1+r3*1], m2
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8l_vertical_left_8(uint8_t *src, int has_topleft,
;                                  int has_topright, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8l_vertical_left_8, 4,4,7
    sub          r0, r3
    movu         m2, [r0-8]
    movu         m1, [r0]
    mova         m3, m1
    psrldq       m2, 7
    psrldq       m1, 1
    test        r1d, r1d
    jnz .check_tr
.fix_lt_2:
    pxor         m5, m3, m2
    psllq        m5, 56
    psrlq        m5, 56
    pxor         m2, m5
.check_tr:
    test        r2d, r2d
    jnz .do_top
.fix_tr_1:
    pxor         m5, m3, m1
    psrlq        m5, 56
    psllq        m5, 56
    pxor         m1, m5
.do_top:
    PRED4x4_LOWPASS  m4, m2, m1, m3, m5
    movq         m6, m4
    jnz .normal_top
.fix_tr_2:
    punpcklbw    m3, m3
    pshufd       m3, m3, q3232
    pshuflw      m1, m3, q3333
    jmp .do_topright
.normal_top:
    movq         m0, [r0+8]
    psrlq        m5, m0, 56
    punpcklqdq   m3, m0
    psrldq       m3, 7
    punpcklqdq   m4, m0, m5
    psrldq       m4, 1
    PRED4x4_LOWPASS  m1, m3, m4, m0, m2
.do_topright:
    lea         r1, [r0+r3*2]
    lea         r2, [r0+r3*4]
    pslldq      m1, 8
    por         m6, m1
    psrldq      m2, m6, 1
    pslldq      m1, m6, 1
    pavgb       m3, m6, m2
    PRED4x4_LOWPASS m6, m1, m2, m6, m5
    psrldq      m6, 1
    movq [r0+r3*1], m3
    movq [r0+r3*2], m6
    lea         r0, [r2+r3*2]
    psrldq      m3, 1
    psrldq      m6, 1
    movq [r1+r3*1], m3
    movq [r1+r3*2], m6
    psrldq      m3, 1
    psrldq      m6, 1
    movq [r2+r3*1], m3
    movq [r2+r3*2], m6
    psrldq      m3, 1
    psrldq      m6, 1
    movq [r0+r3*1], m3
    movq [r0+r3*2], m6
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8l_horizontal_up_8(uint8_t *src, int has_topleft,
;                                  int has_topright, ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred8x8l_horizontal_up_8, 4,4,6
    sub          r0, r3
    lea          r2, [r0+r3*2]
    movd         m0, [r0+r3*1-4]
    test        r1d, r1d
    lea          r1, [r0+r3]
    cmovnz       r1, r0
    movd         m4, [r1+r3*0-4]
    punpcklbw    m0, m4
    movd         m1, [r2+r3*1-4]
    movd         m4, [r0+r3*2-4]
    punpcklbw    m1, m4
    mov          r2, r0
    punpcklwd    m1, m0
    lea          r0, [r0+r3*4]
    movd         m2, [r0+r3*1-4]
    movd         m4, [r0+r3*0-4]
    punpcklbw    m2, m4
    lea          r0, [r0+r3*2]
    movd         m3, [r0+r3*1-4]
    movd         m4, [r0+r3*0-4]
    punpcklbw    m3, m4
    punpcklwd    m3, m2
    punpckhdq    m3, m1
    pshufd       m3, m3, q3232
    lea          r0, [r0+r3*2]
    movq         m0, [r0+r3*0-8]
    movq         m1, [r1+r3*0-8]
    mov          r0, r2
    mova         m2, m3
    punpcklqdq   m0, m3
    psrldq       m0, 7
    punpcklqdq   m2, m1
    psrldq       m2, 1
    mova         m4, m0
    PRED4x4_LOWPASS m1, m2, m4, m3, m5
    mova        m4, m0
    PRED4x4_LOWPASS m2, m3, m0, m4, m5
    psllq       m2, 56
    punpcklqdq  m2, m1
    psrldq      m2, 7
    lea         r1, [r0+r3*2]
    pshuflw     m1, m2, q0123
    psllq       m2, 56
    psllw       m0, m1, 8
    psrlw       m1, 8
    por         m1, m0
    lea         r2, [r1+r3*2]
    mova        m3, m1
    mova        m4, m1
    mova        m5, m1
    psrlq       m1, 8
    por         m1, m2
    pavgb       m4, m1
    psrlq       m3, 16
    punpcklbw   m2, m2
    pshufd      m2, m2, q3232
    por         m3, m2
    PRED4x4_LOWPASS m2, m3, m5, m1, m0
    punpcklbw   m4, m2
    pshufd      m0, m4, q3232
    psrldq      m5, m4, 2
    psrldq      m2, m4, 4
    psrldq      m3, m4, 6
    movq [r0+r3*1], m4
    movq [r0+r3*2], m5
    lea         r0, [r2+r3*2]
    movq [r1+r3*1], m2
    movq [r1+r3*2], m3
    pshuflw     m1, m0, q3321
    pshuflw     m2, m0, q3332
    pshuflw     m3, m0, q3333
    movq [r2+r3*1], m0
    movq [r2+r3*2], m1
    movq [r0+r3*1], m2
    movq [r0+r3*2], m3
    RET

;-----------------------------------------------------------------------------
; void ff_pred8x8l_horizontal_down_8(uint8_t *src, int has_topleft,
;                                    int has_topright, ptrdiff_t stride)
;-----------------------------------------------------------------------------

%macro PRED8x8L_HORIZONTAL_DOWN 0
cglobal pred8x8l_horizontal_down_8, 4,5,8
    sub          r0, r3
    lea          r4, [r0+r3*2]
    movd         m0, [r0+r3*1-4]
    movd         m1, [r0+r3*0-4]
    punpcklbw    m0, m1
    movd         m1, [r4+r3*1-4]
    movd         m2, [r0+r3*2-4]
    punpcklbw    m1, m2
    mov          r4, r0
    punpcklwd    m1, m0
    lea          r0, [r0+r3*4]
    movd         m2, [r0+r3*1-4]
    movd         m3, [r0+r3*0-4]
    punpcklbw    m2, m3
    lea          r0, [r0+r3*2]
    movd         m3, [r0+r3*1-4]
    movd         m4, [r0+r3*0-4]
    punpcklbw    m3, m4
    punpcklwd    m3, m2
    punpckhdq    m3, m1
    pshufd       m3, m3, q3232
    lea          r0, [r0+r3*2]
    movq         m0, [r0+r3*0-8]
    movq         m1, [r4]
    mov          r0, r4
    mova         m2, m3
    punpcklqdq   m0, m3
    psrldq       m0, 7
    mova         m4, m0
    punpcklqdq   m2, m1
    psrldq       m2, 1
    mova         m1, m2
    test        r1d, r1d
    jnz .do_left
.fix_lt_1:
    pxor         m5, m3, m4
    psrlq        m5, 56
    psllq        m5, 48
    pxor         m1, m5
.do_left:
    PRED4x4_LOWPASS  m2, m1, m4, m3, m5
    movq         m6, m2
    pslldq       m6, 8
    mova         m4, m0
    PRED4x4_LOWPASS  m1, m3, m0, m4, m5
    pslldq       m2, m1, 15
    psrldq       m2, 8
    por          m6, m2
    movu         m2, [r0-8]
    movu         m1, [r0]
    mova         m3, m1
    psrldq       m2, 7
    psrldq       m1, 1
    test        r1d, r1d
    jnz .check_r2
.fix_lt_2:
    pxor         m5, m3, m2
    psllq        m5, 56
    psrlq        m5, 56
    pxor         m2, m5
.check_r2:
    test        r2d, r2d
    jnz .do_top
.fix_tr_1:
    pxor         m5, m3, m1
    psrlq        m5, 56
    psllq        m5, 56
    pxor         m1, m5
.do_top:
    PRED4x4_LOWPASS  m7, m2, m1, m3, m5
    test        r2d, r2d
    jz .fix_tr_2
    movq         m0, [r0+8]
    psrlq        m5, m0, 56
    punpcklqdq   m3, m0
    psrldq       m3, 7
    punpcklqdq   m4, m0, m5
    psrldq       m4, 1
    PRED4x4_LOWPASS  m1, m3, m4, m0, m4
.do_topright:
    movq        m0, m1
    pslldq      m0, 8
    por         m7, m0
    lea         r2, [r4+r3*2]
    mova        m2, m7
    mova        m3, m7
    PALIGNR     m7, m6, 7, m4
    PALIGNR     m2, m6, 9, m0
    lea         r1, [r4+r3*4]
    PALIGNR     m3, m6, 8, m6
    pavgb       m4, m7, m3
    lea         r0, [r2+r3*4]
    PRED4x4_LOWPASS m3, m7, m2, m3, m5
    punpcklbw   m4, m3
    movhlps     m3, m4
    movq [r0+r3*2], m4
    movq [r2+r3*2], m3
    psrldq      m4, 2
    psrldq      m3, 2
    movq [r0+r3*1], m4
    movq [r2+r3*1], m3
    psrldq      m4, 2
    psrldq      m3, 2
    movq [r1+r3*2], m4
    movq [r4+r3*2], m3
    psrldq      m4, 2
    psrldq      m3, 2
    movq [r1+r3*1], m4
    movq [r4+r3*1], m3
    RET
.fix_tr_2:
    punpcklbw   m3, m3
    pshufd      m3, m3, q3232
    pshuflw     m1, m3, q3333
    jmp .do_topright
%endmacro

INIT_XMM sse2
PRED8x8L_HORIZONTAL_DOWN
INIT_XMM ssse3
PRED8x8L_HORIZONTAL_DOWN

;-------------------------------------------------------------------------------
; void ff_pred4x4_dc_8_sse2(uint8_t *src, const uint8_t *topright,
;                           ptrdiff_t stride)
;-------------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred4x4_dc_8, 3,5,2
    pxor    m1, m1
    mov     r4, r0
    sub     r0, r2
    movd    m0, [r0]
    psadbw  m0, m1
    movzx  r1d, byte [r0+r2*1-1]
    movd   r3d, m0
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
; void ff_pred4x4_tm_vp8_8_sse2(uint8_t *src, const uint8_t *topright,
;                               ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred4x4_tm_vp8_8, 3,6,4
    sub        r0, r2
    pxor       m1, m1
    movd       m0, [r0]
    punpcklbw  m0, m1
    movzx     r4d, byte [r0-1]
    mov       r5d, 2
.loop:
    movzx     r1d, byte [r0+r2*1-1]
    movzx     r3d, byte [r0+r2*2-1]
    sub       r1d, r4d
    sub       r3d, r4d
    movd       m2, r1d
    movd       m3, r3d
    pshuflw    m2, m2, 0
    pshuflw    m3, m3, 0
    paddw      m2, m0
    paddw      m3, m0
    packuswb   m2, m2
    packuswb   m3, m3
    movd [r0+r2*1], m2
    movd [r0+r2*2], m3
    lea        r0, [r0+r2*2]
    dec       r5d
    jg .loop
    RET

INIT_XMM ssse3
cglobal pred4x4_tm_vp8_8, 3,3,6
    sub         r0, r2
    movq        m1, [tm_shuf]
    movd        m0, [r0]
    movd        m5, [r0-4]
    lea         r1, [r0+r2*2]
    pxor        m4, m4
    punpcklbw   m0, m4
    pshufb      m5, m1
    movd        m2, [r0+r2*1-4]
    movd        m3, [r0+r2*2-4]
    movd        m4, [r1+r2*1-4]
    psubw       m0, m5
    movd        m5, [r1+r2*2-4]
    pshufb      m2, m1
    pshufb      m3, m1
    pshufb      m4, m1
    pshufb      m5, m1
    paddw       m2, m0
    paddw       m3, m0
    paddw       m4, m0
    paddw       m5, m0
    packuswb    m2, m2
    packuswb    m3, m3
    packuswb    m4, m4
    packuswb    m5, m5
    movd [r0+r2*1], m2
    movd [r0+r2*2], m3
    movd [r1+r2*1], m4
    movd [r1+r2*2], m5
    RET

;-----------------------------------------------------------------------------
; void ff_pred4x4_vertical_vp8_8_sse2(uint8_t *src, const uint8_t *topright,
;                                     ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred4x4_vertical_vp8_8, 3,3
    sub       r0, r2
    movd      m1, [r0-1]
    movd      m0, [r0]
    mova      m2, m0   ;t0 t1 t2 t3
    movq      m4, [r1]
    punpckldq m0, m4 ;t0 t1 t2 t3 t4 t5 t6 t7
    lea       r1, [r0+r2*2]
    psrlq     m0, 8    ;t1 t2 t3 t4
    PRED4x4_LOWPASS m2, m1, m0, m2, m4
    movd [r0+r2*1], m2
    movd [r0+r2*2], m2
    movd [r1+r2*1], m2
    movd [r1+r2*2], m2
    RET

;-----------------------------------------------------------------------------
; void ff_pred4x4_down_left_8_sse2(uint8_t *src, const uint8_t *topright,
;                                  ptrdiff_t stride)
;-----------------------------------------------------------------------------
INIT_XMM sse2
cglobal pred4x4_down_left_8, 3,3,5
    sub       r0, r2
    movq      m1, [r0]
    movq      m2, [r1]
    punpckldq m1, m2
    movq      m2, m1
    movq      m0, m1
    psllq     m1, 8
    pxor      m2, m1
    psrlq     m2, 8
    pxor      m2, m0
    PRED4x4_LOWPASS m0, m1, m2, m0, m3
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

;------------------------------------------------------------------------------
; void ff_pred4x4_vertical_left_8_sse2(uint8_t *src, const uint8_t *topright,
;                                      ptrdiff_t stride)
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred4x4_vertical_left_8, 3,3,6
    sub       r0, r2
    movq      m1, [r0]
    movq      m2, [r1]
    punpckldq m1, m2
    movq      m0, m1
    movq      m2, m1
    psrlq     m0, 8
    psrlq     m2, 16
    movq      m4, m0
    pavgb     m4, m1
    PRED4x4_LOWPASS m0, m1, m2, m0, m5
    lea       r1, [r0+r2*2]
    movd      [r0+r2*1], m4
    movd      [r0+r2*2], m0
    psrlq     m4, 8
    psrlq     m0, 8
    movd      [r1+r2*1], m4
    movd      [r1+r2*2], m0
    RET

;------------------------------------------------------------------------------
; void ff_pred4x4_horizontal_up_8_sse2(uint8_t *src, const uint8_t *topright,
;                                      ptrdiff_t stride)
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred4x4_horizontal_up_8, 3,3,7
    sub       r0, r2
    lea       r1, [r0+r2*2]
    movd      m0, [r0+r2*1-4]
    movd      m1, [r0+r2*2-4]
    punpcklbw m0, m1
    movd      m1, [r1+r2*1-4]
    movd      m2, [r1+r2*2-4]
    punpcklbw m1, m2
    punpcklwd m0, m1
    pshufd    m0, m0, q3333
    mova      m1, m0
    punpcklbw m1, m1
    pshuflw   m1, m1, q3333
    punpckldq m0, m1
    mova      m2, m0
    mova      m3, m0
    mova      m6, m0
    psrlq     m2, 16
    psrlq     m3, 8
    pavgb     m6, m3
    PRED4x4_LOWPASS m4, m0, m2, m3, m5
    punpcklbw m6, m4
    movd    [r0+r2*1], m6
    psrlq    m6, 16
    movd    [r0+r2*2], m6
    psrlq    m6, 16
    movd    [r1+r2*1], m6
    movd    [r1+r2*2], m1
    RET

;------------------------------------------------------------------------------
; void ff_pred4x4_horizontal_down_8_sse2(uint8_t *src,
;                                        const uint8_t *topright,
;                                        ptrdiff_t stride)
;------------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred4x4_horizontal_down_8, 3,3,6
    sub       r0, r2
    lea       r1, [r0+r2*2]
    pxor      m0, m0
    movhps    m0, [r0-4]      ; t3 t2 t1 t0 lt .. .. ..
    psllq     m0, 8           ; t2 t1 t0 lt .. .. .. ..
    movd      m1, [r1+r2*2-4] ; l3
    movd      m2, [r1+r2*1-4]
    punpcklbw m1, m2          ; l2 l3
    movd      m2, [r0+r2*2-4] ; l1
    movd      m3, [r0+r2*1-4]
    punpcklbw m2, m3          ; l0 l1
    punpcklwd m1, m2          ; l0 l1 l2 l3
    punpckhdq m1, m0          ; t2 t1 t0 lt l0 l1 l2 l3
    pshufd    m1, m1, q3232
    psrlq     m0, m1, 16      ; .. .. t2 t1 t0 lt l0 l1
    psrlq     m2, m1, 8       ; .. t2 t1 t0 lt l0 l1 l2
    pavgb     m5, m1, m2
    PRED4x4_LOWPASS m3, m1, m0, m2, m4
    punpcklbw  m5, m3
    psrlq      m3, 32
    mova       m0, m5
    punpcklqdq m5, m3
    psrldq     m5, 6
    movd       [r1+r2*2], m0
    psrlq      m0, 16
    movd       [r1+r2*1], m0
    psrlq      m0, 16
    movd       [r0+r2*2], m0
    movd       [r0+r2*1], m5
    RET

;-----------------------------------------------------------------------------
; void ff_pred4x4_vertical_right_8_sse2(uint8_t *src,
;                                       const uint8_t *topright,
;                                       ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred4x4_vertical_right_8, 3,3,6
    sub     r0, r2
    lea     r1, [r0+r2*2]
    movd    m5, [r0]                    ; ........t3t2t1t0
    movu    m1, [r0-8]                  ; .....t3t2t1t0lt.
    psrldq  m1, 7                       ; ......t3t2t1t0lt
    pavgb   m5, m1
    movq    m2, [r0+r2*1-8]
    punpcklqdq m2, m1
    psrldq  m2, 7                       ; ....t3t2t1t0ltl0
    mova    m1, m2
    movq    m3, [r0+r2*2-8]
    punpcklqdq m3, m2
    psrldq  m3, 7                       ; ..t3t2t1t0ltl0l1
    movq    m0, [r1+r2*1-8]
    punpcklqdq m0, m3
    psrldq  m0, 7                       ; t3t2t1t0ltl0l1l2
    PRED4x4_LOWPASS m3, m1, m0, m3, m4
    mova    m1, m3
    psrlq   m3, 16
    psllq   m1, 48
    movd    [r0+r2*1], m5
    movd    [r0+r2*2], m3
    mova    m2, m1
    punpcklqdq m1, m5
    psrldq  m1, 7
    psllq   m2, 8
    movd    [r1+r2*1], m1
    punpcklqdq m2, m3
    psrldq  m2, 7
    movd    [r1+r2*2], m2
    RET

;-----------------------------------------------------------------------------
; void ff_pred4x4_down_right_8_sse2(uint8_t *src, const uint8_t *topright,
;                                   ptrdiff_t stride)
;-----------------------------------------------------------------------------

INIT_XMM sse2
cglobal pred4x4_down_right_8, 3,3,5
    sub       r0, r2
    lea       r1, [r0+r2*2]
    movq      m1, [r1-8]
    movd      m2, [r0+r2*1-4]
    movd      m3, [r0-4]
    punpcklbw m2, m3
    movd      m3, [r0]
    punpcklwd m1, m2
    pshufd    m1, m1, q3232
    punpcklqdq m1, m3
    psrldq     m1, 5
    movq       m3, [r1+r2*1-8]
    punpcklqdq m3, m1
    psrldq     m3, 7
    movq       m2, [r1+r2*2-8]
    punpcklqdq m2, m3
    psrldq     m2, 7
    PRED4x4_LOWPASS m0, m2, m1, m3, m4
    movd      [r1+r2*2], m0
    psrlq     m0, 8
    movd      [r1+r2*1], m0
    psrlq     m0, 8
    movd      [r0+r2*2], m0
    psrlq     m0, 8
    movd      [r0+r2*1], m0
    RET
