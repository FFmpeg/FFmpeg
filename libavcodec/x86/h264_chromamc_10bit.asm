;*****************************************************************************
;* MMX/SSE2/AVX-optimized 10-bit H.264 chroma MC code
;*****************************************************************************
;* Copyright (C) 2005-2011 x264 project
;*
;* Authors: Daniel Kang <daniel.d.kang@gmail.com>
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

cextern pw_4
cextern pw_8
cextern pw_32
cextern pw_64

SECTION .text


%macro MV0_PIXELS_MC8 0
    lea           r4, [r2*3   ]
    lea           r5, [r2*4   ]
.next4rows:
    movu          m0, [r1     ]
    movu          m1, [r1+r2  ]
    CHROMAMC_AVG  m0, [r0     ]
    CHROMAMC_AVG  m1, [r0+r2  ]
    mova   [r0     ], m0
    mova   [r0+r2  ], m1
    movu          m0, [r1+r2*2]
    movu          m1, [r1+r4  ]
    CHROMAMC_AVG  m0, [r0+r2*2]
    CHROMAMC_AVG  m1, [r0+r4  ]
    mova   [r0+r2*2], m0
    mova   [r0+r4  ], m1
    add           r1, r5
    add           r0, r5
    sub          r3d, 4
    jne .next4rows
%endmacro

;-----------------------------------------------------------------------------
; void put/avg_h264_chroma_mc8(pixel *dst, pixel *src, int stride, int h, int mx, int my)
;-----------------------------------------------------------------------------
%macro CHROMA_MC8 1
; put/avg_h264_chroma_mc8_*(uint8_t *dst /*align 8*/, uint8_t *src /*align 1*/,
;                              int stride, int h, int mx, int my)
cglobal %1_h264_chroma_mc8_10, 6,7,8
    movsxdifnidn  r2, r2d
    mov          r6d, r5d
    or           r6d, r4d
    jne .at_least_one_non_zero
    ; mx == 0 AND my == 0 - no filter needed
    MV0_PIXELS_MC8
    REP_RET

.at_least_one_non_zero:
    mov          r6d, 2
    test         r5d, r5d
    je .x_interpolation
    mov           r6, r2        ; dxy = x ? 1 : stride
    test         r4d, r4d
    jne .xy_interpolation
.x_interpolation:
    ; mx == 0 XOR my == 0 - 1 dimensional filter only
    or           r4d, r5d       ; x + y
    movd          m5, r4d
    mova          m4, [pw_8]
    mova          m6, [pw_4]    ; mm6 = rnd >> 3
    SPLATW        m5, m5        ; mm5 = B = x
    psubw         m4, m5        ; mm4 = A = 8-x

.next1drow:
    movu          m0, [r1   ]   ; mm0 = src[0..7]
    movu          m2, [r1+r6]   ; mm2 = src[1..8]

    pmullw        m0, m4        ; mm0 = A * src[0..7]
    pmullw        m2, m5        ; mm2 = B * src[1..8]

    paddw         m0, m6
    paddw         m0, m2
    psrlw         m0, 3
    CHROMAMC_AVG  m0, [r0]
    mova        [r0], m0        ; dst[0..7] = (A * src[0..7] + B * src[1..8] + (rnd >> 3)) >> 3

    add           r0, r2
    add           r1, r2
    dec           r3d
    jne .next1drow
    REP_RET

.xy_interpolation: ; general case, bilinear
    movd          m4, r4m         ; x
    movd          m6, r5m         ; y

    SPLATW        m4, m4          ; mm4 = x words
    SPLATW        m6, m6          ; mm6 = y words
    psllw         m5, m4, 3       ; mm5 = 8x
    pmullw        m4, m6          ; mm4 = x * y
    psllw         m6, 3           ; mm6 = 8y
    paddw         m1, m5, m6      ; mm7 = 8x+8y
    mova          m7, m4          ; DD = x * y
    psubw         m5, m4          ; mm5 = B = 8x - xy
    psubw         m6, m4          ; mm6 = C = 8y - xy
    paddw         m4, [pw_64]
    psubw         m4, m1          ; mm4 = A = xy - (8x+8y) + 64

    movu          m0, [r1  ]      ; mm0 = src[0..7]
    movu          m1, [r1+2]      ; mm1 = src[1..8]
.next2drow:
    add           r1, r2

    pmullw        m2, m0, m4
    pmullw        m1, m5
    paddw         m2, m1          ; mm2 = A * src[0..7] + B * src[1..8]

    movu          m0, [r1]
    movu          m1, [r1+2]
    pmullw        m3, m0, m6
    paddw         m2, m3          ; mm2 += C * src[0..7+strde]
    pmullw        m3, m1, m7
    paddw         m2, m3          ; mm2 += D * src[1..8+strde]

    paddw         m2, [pw_32]
    psrlw         m2, 6
    CHROMAMC_AVG  m2, [r0]
    mova        [r0], m2          ; dst[0..7] = (mm2 + 32) >> 6

    add           r0, r2
    dec          r3d
    jne .next2drow
    REP_RET
%endmacro

;-----------------------------------------------------------------------------
; void put/avg_h264_chroma_mc4(pixel *dst, pixel *src, int stride, int h, int mx, int my)
;-----------------------------------------------------------------------------
;TODO: xmm mc4
%macro MC4_OP 2
    movq          %1, [r1  ]
    movq          m1, [r1+2]
    add           r1, r2
    pmullw        %1, m4
    pmullw        m1, m2
    paddw         m1, %1
    mova          %1, m1

    pmullw        %2, m5
    pmullw        m1, m3
    paddw         %2, [pw_32]
    paddw         m1, %2
    psrlw         m1, 6
    CHROMAMC_AVG  m1, %2, [r0]
    movq        [r0], m1
    add           r0, r2
%endmacro

%macro CHROMA_MC4 1
cglobal %1_h264_chroma_mc4_10, 6,6,7
    movsxdifnidn  r2, r2d
    movd          m2, r4m         ; x
    movd          m3, r5m         ; y
    mova          m4, [pw_8]
    mova          m5, m4
    SPLATW        m2, m2
    SPLATW        m3, m3
    psubw         m4, m2
    psubw         m5, m3

    movq          m0, [r1  ]
    movq          m6, [r1+2]
    add           r1, r2
    pmullw        m0, m4
    pmullw        m6, m2
    paddw         m6, m0

.next2rows:
    MC4_OP m0, m6
    MC4_OP m6, m0
    sub   r3d, 2
    jnz .next2rows
    REP_RET
%endmacro

;-----------------------------------------------------------------------------
; void put/avg_h264_chroma_mc2(pixel *dst, pixel *src, int stride, int h, int mx, int my)
;-----------------------------------------------------------------------------
%macro CHROMA_MC2 1
cglobal %1_h264_chroma_mc2_10, 6,7
    movsxdifnidn  r2, r2d
    mov          r6d, r4d
    shl          r4d, 16
    sub          r4d, r6d
    add          r4d, 8
    imul         r5d, r4d         ; x*y<<16 | y*(8-x)
    shl          r4d, 3
    sub          r4d, r5d         ; x*(8-y)<<16 | (8-x)*(8-y)

    movd          m5, r4d
    movd          m6, r5d
    punpckldq     m5, m5          ; mm5 = {A,B,A,B}
    punpckldq     m6, m6          ; mm6 = {C,D,C,D}
    pxor          m7, m7
    pshufw        m2, [r1], 0x94    ; mm0 = src[0,1,1,2]

.nextrow:
    add           r1, r2
    movq          m1, m2
    pmaddwd       m1, m5          ; mm1 = A * src[0,1] + B * src[1,2]
    pshufw        m0, [r1], 0x94    ; mm0 = src[0,1,1,2]
    movq          m2, m0
    pmaddwd       m0, m6
    paddw         m1, [pw_32]
    paddw         m1, m0          ; mm1 += C * src[0,1] + D * src[1,2]
    psrlw         m1, 6
    packssdw      m1, m7
    CHROMAMC_AVG  m1, m3, [r0]
    movd        [r0], m1
    add           r0, r2
    dec          r3d
    jnz .nextrow
    REP_RET
%endmacro

%macro NOTHING 2-3
%endmacro
%macro AVG 2-3
%if %0==3
    movq          %2, %3
%endif
    PAVG          %1, %2
%endmacro

%define CHROMAMC_AVG  NOTHING
INIT_XMM sse2
CHROMA_MC8 put
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CHROMA_MC8 put
%endif
INIT_MMX mmx2
CHROMA_MC4 put
CHROMA_MC2 put

%define CHROMAMC_AVG  AVG
%define PAVG          pavgw
INIT_XMM sse2
CHROMA_MC8 avg
%if HAVE_AVX_EXTERNAL
INIT_XMM avx
CHROMA_MC8 avg
%endif
INIT_MMX mmx2
CHROMA_MC4 avg
CHROMA_MC2 avg
