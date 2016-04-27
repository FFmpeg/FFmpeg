;******************************************************************************
;* MMX/SSSE3-optimized functions for H.264 chroma MC
;* Copyright (c) 2005 Zoltan Hidvegi <hzoli -a- hzoli -d- com>,
;*               2005-2008 Loren Merritt
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

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

rnd_rv40_2d_tbl: times 4 dw  0
                 times 4 dw 16
                 times 4 dw 32
                 times 4 dw 16
                 times 4 dw 32
                 times 4 dw 28
                 times 4 dw 32
                 times 4 dw 28
                 times 4 dw  0
                 times 4 dw 32
                 times 4 dw 16
                 times 4 dw 32
                 times 4 dw 32
                 times 4 dw 28
                 times 4 dw 32
                 times 4 dw 28
rnd_rv40_1d_tbl: times 4 dw  0
                 times 4 dw  2
                 times 4 dw  4
                 times 4 dw  2
                 times 4 dw  4
                 times 4 dw  3
                 times 4 dw  4
                 times 4 dw  3
                 times 4 dw  0
                 times 4 dw  4
                 times 4 dw  2
                 times 4 dw  4
                 times 4 dw  4
                 times 4 dw  3
                 times 4 dw  4
                 times 4 dw  3

cextern pw_3
cextern pw_4
cextern pw_8
pw_28: times 8 dw 28
cextern pw_32
cextern pw_64

SECTION .text

%macro mv0_pixels_mc8 0
    lea           r4, [r2*2 ]
.next4rows:
    movq         mm0, [r1   ]
    movq         mm1, [r1+r2]
    add           r1, r4
    CHROMAMC_AVG mm0, [r0   ]
    CHROMAMC_AVG mm1, [r0+r2]
    movq     [r0   ], mm0
    movq     [r0+r2], mm1
    add           r0, r4
    movq         mm0, [r1   ]
    movq         mm1, [r1+r2]
    add           r1, r4
    CHROMAMC_AVG mm0, [r0   ]
    CHROMAMC_AVG mm1, [r0+r2]
    movq     [r0   ], mm0
    movq     [r0+r2], mm1
    add           r0, r4
    sub          r3d, 4
    jne .next4rows
%endmacro

%macro chroma_mc8_mmx_func 2-3
%ifidn %2, rv40
%ifdef PIC
%define rnd_1d_rv40 r8
%define rnd_2d_rv40 r8
%define extra_regs 2
%else ; no-PIC
%define rnd_1d_rv40 rnd_rv40_1d_tbl
%define rnd_2d_rv40 rnd_rv40_2d_tbl
%define extra_regs 1
%endif ; PIC
%else
%define extra_regs 0
%endif ; rv40
; void ff_put/avg_h264_chroma_mc8_*(uint8_t *dst /* align 8 */,
;                                   uint8_t *src /* align 1 */,
;                                   int stride, int h, int mx, int my)
cglobal %1_%2_chroma_mc8%3, 6, 7 + extra_regs, 0
%if ARCH_X86_64
    movsxd        r2, r2d
%endif
    mov          r6d, r5d
    or           r6d, r4d
    jne .at_least_one_non_zero
    ; mx == 0 AND my == 0 - no filter needed
    mv0_pixels_mc8
    REP_RET

.at_least_one_non_zero:
%ifidn %2, rv40
%if ARCH_X86_64
    mov           r7, r5
    and           r7, 6         ; &~1 for mx/my=[0,7]
    lea           r7, [r7*4+r4]
    sar          r7d, 1
%define rnd_bias r7
%define dest_reg r0
%else ; x86-32
    mov           r0, r5
    and           r0, 6         ; &~1 for mx/my=[0,7]
    lea           r0, [r0*4+r4]
    sar          r0d, 1
%define rnd_bias r0
%define dest_reg r5
%endif
%else ; vc1, h264
%define rnd_bias  0
%define dest_reg r0
%endif

    test         r5d, r5d
    mov           r6, 1
    je .my_is_zero
    test         r4d, r4d
    mov           r6, r2        ; dxy = x ? 1 : stride
    jne .both_non_zero
.my_is_zero:
    ; mx == 0 XOR my == 0 - 1 dimensional filter only
    or           r4d, r5d       ; x + y

%ifidn %2, rv40
%ifdef PIC
    lea           r8, [rnd_rv40_1d_tbl]
%endif
%if ARCH_X86_64 == 0
    mov           r5, r0m
%endif
%endif

    movd          m5, r4d
    movq          m4, [pw_8]
    movq          m6, [rnd_1d_%2+rnd_bias*8] ; mm6 = rnd >> 3
    punpcklwd     m5, m5
    punpckldq     m5, m5        ; mm5 = B = x
    pxor          m7, m7
    psubw         m4, m5        ; mm4 = A = 8-x

.next1drow:
    movq          m0, [r1   ]   ; mm0 = src[0..7]
    movq          m2, [r1+r6]   ; mm1 = src[1..8]

    movq          m1, m0
    movq          m3, m2
    punpcklbw     m0, m7
    punpckhbw     m1, m7
    punpcklbw     m2, m7
    punpckhbw     m3, m7
    pmullw        m0, m4        ; [mm0,mm1] = A * src[0..7]
    pmullw        m1, m4
    pmullw        m2, m5        ; [mm2,mm3] = B * src[1..8]
    pmullw        m3, m5

    paddw         m0, m6
    paddw         m1, m6
    paddw         m0, m2
    paddw         m1, m3
    psrlw         m0, 3
    psrlw         m1, 3
    packuswb      m0, m1
    CHROMAMC_AVG  m0, [dest_reg]
    movq  [dest_reg], m0        ; dst[0..7] = (A * src[0..7] + B * src[1..8] + (rnd >> 3)) >> 3

    add     dest_reg, r2
    add           r1, r2
    dec           r3d
    jne .next1drow
    REP_RET

.both_non_zero: ; general case, bilinear
    movd          m4, r4d         ; x
    movd          m6, r5d         ; y
%ifidn %2, rv40
%ifdef PIC
    lea           r8, [rnd_rv40_2d_tbl]
%endif
%if ARCH_X86_64 == 0
    mov           r5, r0m
%endif
%endif
    mov           r6, rsp         ; backup stack pointer
    and          rsp, ~(mmsize-1) ; align stack
    sub          rsp, 16          ; AA and DD

    punpcklwd     m4, m4
    punpcklwd     m6, m6
    punpckldq     m4, m4          ; mm4 = x words
    punpckldq     m6, m6          ; mm6 = y words
    movq          m5, m4
    pmullw        m4, m6          ; mm4 = x * y
    psllw         m5, 3
    psllw         m6, 3
    movq          m7, m5
    paddw         m7, m6
    movq     [rsp+8], m4          ; DD = x * y
    psubw         m5, m4          ; mm5 = B = 8x - xy
    psubw         m6, m4          ; mm6 = C = 8y - xy
    paddw         m4, [pw_64]
    psubw         m4, m7          ; mm4 = A = xy - (8x+8y) + 64
    pxor          m7, m7
    movq     [rsp  ], m4

    movq          m0, [r1  ]      ; mm0 = src[0..7]
    movq          m1, [r1+1]      ; mm1 = src[1..8]
.next2drow:
    add           r1, r2

    movq          m2, m0
    movq          m3, m1
    punpckhbw     m0, m7
    punpcklbw     m1, m7
    punpcklbw     m2, m7
    punpckhbw     m3, m7
    pmullw        m0, [rsp]
    pmullw        m2, [rsp]
    pmullw        m1, m5
    pmullw        m3, m5
    paddw         m2, m1          ; mm2 = A * src[0..3] + B * src[1..4]
    paddw         m3, m0          ; mm3 = A * src[4..7] + B * src[5..8]

    movq          m0, [r1]
    movq          m1, m0
    punpcklbw     m0, m7
    punpckhbw     m1, m7
    pmullw        m0, m6
    pmullw        m1, m6
    paddw         m2, m0
    paddw         m3, m1          ; [mm2,mm3] += C * src[0..7]

    movq          m1, [r1+1]
    movq          m0, m1
    movq          m4, m1
    punpcklbw     m0, m7
    punpckhbw     m4, m7
    pmullw        m0, [rsp+8]
    pmullw        m4, [rsp+8]
    paddw         m2, m0
    paddw         m3, m4          ; [mm2,mm3] += D * src[1..8]
    movq          m0, [r1]

    paddw         m2, [rnd_2d_%2+rnd_bias*8]
    paddw         m3, [rnd_2d_%2+rnd_bias*8]
    psrlw         m2, 6
    psrlw         m3, 6
    packuswb      m2, m3
    CHROMAMC_AVG  m2, [dest_reg]
    movq  [dest_reg], m2          ; dst[0..7] = ([mm2,mm3] + rnd) >> 6

    add     dest_reg, r2
    dec          r3d
    jne .next2drow
    mov          rsp, r6          ; restore stack pointer
    RET
%endmacro

%macro chroma_mc4_mmx_func 2
%define extra_regs 0
%ifidn %2, rv40
%ifdef PIC
%define extra_regs 1
%endif ; PIC
%endif ; rv40
cglobal %1_%2_chroma_mc4, 6, 6 + extra_regs, 0
%if ARCH_X86_64
    movsxd        r2, r2d
%endif
    pxor          m7, m7
    movd          m2, r4d         ; x
    movd          m3, r5d         ; y
    movq          m4, [pw_8]
    movq          m5, [pw_8]
    punpcklwd     m2, m2
    punpcklwd     m3, m3
    punpcklwd     m2, m2
    punpcklwd     m3, m3
    psubw         m4, m2
    psubw         m5, m3

%ifidn %2, rv40
%ifdef PIC
   lea            r6, [rnd_rv40_2d_tbl]
%define rnd_2d_rv40 r6
%else
%define rnd_2d_rv40 rnd_rv40_2d_tbl
%endif
    and           r5, 6         ; &~1 for mx/my=[0,7]
    lea           r5, [r5*4+r4]
    sar          r5d, 1
%define rnd_bias r5
%else ; vc1, h264
%define rnd_bias 0
%endif

    movd          m0, [r1  ]
    movd          m6, [r1+1]
    add           r1, r2
    punpcklbw     m0, m7
    punpcklbw     m6, m7
    pmullw        m0, m4
    pmullw        m6, m2
    paddw         m6, m0

.next2rows:
    movd          m0, [r1  ]
    movd          m1, [r1+1]
    add           r1, r2
    punpcklbw     m0, m7
    punpcklbw     m1, m7
    pmullw        m0, m4
    pmullw        m1, m2
    paddw         m1, m0
    movq          m0, m1

    pmullw        m6, m5
    pmullw        m1, m3
    paddw         m6, [rnd_2d_%2+rnd_bias*8]
    paddw         m1, m6
    psrlw         m1, 6
    packuswb      m1, m1
    CHROMAMC_AVG4 m1, m6, [r0]
    movd        [r0], m1
    add           r0, r2

    movd          m6, [r1  ]
    movd          m1, [r1+1]
    add           r1, r2
    punpcklbw     m6, m7
    punpcklbw     m1, m7
    pmullw        m6, m4
    pmullw        m1, m2
    paddw         m1, m6
    movq          m6, m1
    pmullw        m0, m5
    pmullw        m1, m3
    paddw         m0, [rnd_2d_%2+rnd_bias*8]
    paddw         m1, m0
    psrlw         m1, 6
    packuswb      m1, m1
    CHROMAMC_AVG4 m1, m0, [r0]
    movd        [r0], m1
    add           r0, r2
    sub          r3d, 2
    jnz .next2rows
    REP_RET
%endmacro

%macro chroma_mc2_mmx_func 2
cglobal %1_%2_chroma_mc2, 6, 7, 0
%if ARCH_X86_64
    movsxd        r2, r2d
%endif

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
    movd          m2, [r1]
    punpcklbw     m2, m7
    pshufw        m2, m2, 0x94    ; mm0 = src[0,1,1,2]

.nextrow:
    add           r1, r2
    movq          m1, m2
    pmaddwd       m1, m5          ; mm1 = A * src[0,1] + B * src[1,2]
    movd          m0, [r1]
    punpcklbw     m0, m7
    pshufw        m0, m0, 0x94    ; mm0 = src[0,1,1,2]
    movq          m2, m0
    pmaddwd       m0, m6
    paddw         m1, [rnd_2d_%2]
    paddw         m1, m0          ; mm1 += C * src[0,1] + D * src[1,2]
    psrlw         m1, 6
    packssdw      m1, m7
    packuswb      m1, m7
    CHROMAMC_AVG4 m1, m3, [r0]
    movd         r5d, m1
    mov         [r0], r5w
    add           r0, r2
    sub          r3d, 1
    jnz .nextrow
    REP_RET
%endmacro

%define rnd_1d_h264 pw_4
%define rnd_2d_h264 pw_32
%define rnd_1d_vc1  pw_3
%define rnd_2d_vc1  pw_28

%macro NOTHING 2-3
%endmacro
%macro DIRECT_AVG 2
    PAVGB         %1, %2
%endmacro
%macro COPY_AVG 3
    movd          %2, %3
    PAVGB         %1, %2
%endmacro

INIT_MMX mmx
%define CHROMAMC_AVG  NOTHING
%define CHROMAMC_AVG4 NOTHING
chroma_mc8_mmx_func put, h264, _rnd
chroma_mc8_mmx_func put, vc1,  _nornd
chroma_mc8_mmx_func put, rv40
chroma_mc4_mmx_func put, h264
chroma_mc4_mmx_func put, rv40

INIT_MMX mmxext
chroma_mc2_mmx_func put, h264

%define CHROMAMC_AVG  DIRECT_AVG
%define CHROMAMC_AVG4 COPY_AVG
chroma_mc8_mmx_func avg, h264, _rnd
chroma_mc8_mmx_func avg, vc1,  _nornd
chroma_mc8_mmx_func avg, rv40
chroma_mc4_mmx_func avg, h264
chroma_mc4_mmx_func avg, rv40
chroma_mc2_mmx_func avg, h264

INIT_MMX 3dnow
chroma_mc8_mmx_func avg, h264, _rnd
chroma_mc8_mmx_func avg, vc1,  _nornd
chroma_mc8_mmx_func avg, rv40
chroma_mc4_mmx_func avg, h264
chroma_mc4_mmx_func avg, rv40

%macro chroma_mc8_ssse3_func 2-3
cglobal %1_%2_chroma_mc8%3, 6, 7, 8
%if ARCH_X86_64
    movsxd        r2, r2d
%endif
    mov          r6d, r5d
    or           r6d, r4d
    jne .at_least_one_non_zero
    ; mx == 0 AND my == 0 - no filter needed
    mv0_pixels_mc8
    REP_RET

.at_least_one_non_zero:
    test         r5d, r5d
    je .my_is_zero
    test         r4d, r4d
    je .mx_is_zero

    ; general case, bilinear
    mov          r6d, r4d
    shl          r4d, 8
    sub           r4, r6
    mov           r6, 8
    add           r4, 8           ; x*288+8 = x<<8 | (8-x)
    sub          r6d, r5d
    imul          r6, r4          ; (8-y)*(x*255+8) = (8-y)*x<<8 | (8-y)*(8-x)
    imul         r4d, r5d         ;    y *(x*255+8) =    y *x<<8 |    y *(8-x)

    movd          m7, r6d
    movd          m6, r4d
    movdqa        m5, [rnd_2d_%2]
    movq          m0, [r1  ]
    movq          m1, [r1+1]
    pshuflw       m7, m7, 0
    pshuflw       m6, m6, 0
    punpcklbw     m0, m1
    movlhps       m7, m7
    movlhps       m6, m6

.next2rows:
    movq          m1, [r1+r2*1   ]
    movq          m2, [r1+r2*1+1]
    movq          m3, [r1+r2*2  ]
    movq          m4, [r1+r2*2+1]
    lea           r1, [r1+r2*2]
    punpcklbw     m1, m2
    movdqa        m2, m1
    punpcklbw     m3, m4
    movdqa        m4, m3
    pmaddubsw     m0, m7
    pmaddubsw     m1, m6
    pmaddubsw     m2, m7
    pmaddubsw     m3, m6
    paddw         m0, m5
    paddw         m2, m5
    paddw         m1, m0
    paddw         m3, m2
    psrlw         m1, 6
    movdqa        m0, m4
    psrlw         m3, 6
%ifidn %1, avg
    movq          m2, [r0   ]
    movhps        m2, [r0+r2]
%endif
    packuswb      m1, m3
    CHROMAMC_AVG  m1, m2
    movq     [r0   ], m1
    movhps   [r0+r2], m1
    sub          r3d, 2
    lea           r0, [r0+r2*2]
    jg .next2rows
    REP_RET

.my_is_zero:
    mov          r5d, r4d
    shl          r4d, 8
    add           r4, 8
    sub           r4, r5          ; 255*x+8 = x<<8 | (8-x)
    movd          m7, r4d
    movdqa        m6, [rnd_1d_%2]
    pshuflw       m7, m7, 0
    movlhps       m7, m7

.next2xrows:
    movq          m0, [r1     ]
    movq          m1, [r1   +1]
    movq          m2, [r1+r2  ]
    movq          m3, [r1+r2+1]
    punpcklbw     m0, m1
    punpcklbw     m2, m3
    pmaddubsw     m0, m7
    pmaddubsw     m2, m7
%ifidn %1, avg
    movq          m4, [r0   ]
    movhps        m4, [r0+r2]
%endif
    paddw         m0, m6
    paddw         m2, m6
    psrlw         m0, 3
    psrlw         m2, 3
    packuswb      m0, m2
    CHROMAMC_AVG  m0, m4
    movq     [r0   ], m0
    movhps   [r0+r2], m0
    sub          r3d, 2
    lea           r0, [r0+r2*2]
    lea           r1, [r1+r2*2]
    jg .next2xrows
    REP_RET

.mx_is_zero:
    mov          r4d, r5d
    shl          r5d, 8
    add           r5, 8
    sub           r5, r4          ; 255*y+8 = y<<8 | (8-y)
    movd          m7, r5d
    movdqa        m6, [rnd_1d_%2]
    pshuflw       m7, m7, 0
    movlhps       m7, m7

.next2yrows:
    movq          m0, [r1     ]
    movq          m1, [r1+r2  ]
    movdqa        m2, m1
    movq          m3, [r1+r2*2]
    lea           r1, [r1+r2*2]
    punpcklbw     m0, m1
    punpcklbw     m2, m3
    pmaddubsw     m0, m7
    pmaddubsw     m2, m7
%ifidn %1, avg
    movq          m4, [r0   ]
    movhps        m4, [r0+r2]
%endif
    paddw         m0, m6
    paddw         m2, m6
    psrlw         m0, 3
    psrlw         m2, 3
    packuswb      m0, m2
    CHROMAMC_AVG  m0, m4
    movq     [r0   ], m0
    movhps   [r0+r2], m0
    sub          r3d, 2
    lea           r0, [r0+r2*2]
    jg .next2yrows
    REP_RET
%endmacro

%macro chroma_mc4_ssse3_func 2
cglobal %1_%2_chroma_mc4, 6, 7, 0
%if ARCH_X86_64
    movsxd        r2, r2d
%endif
    mov           r6, r4
    shl          r4d, 8
    sub          r4d, r6d
    mov           r6, 8
    add          r4d, 8           ; x*288+8
    sub          r6d, r5d
    imul         r6d, r4d         ; (8-y)*(x*255+8) = (8-y)*x<<8 | (8-y)*(8-x)
    imul         r4d, r5d         ;    y *(x*255+8) =    y *x<<8 |    y *(8-x)

    movd          m7, r6d
    movd          m6, r4d
    movq          m5, [pw_32]
    movd          m0, [r1  ]
    pshufw        m7, m7, 0
    punpcklbw     m0, [r1+1]
    pshufw        m6, m6, 0

.next2rows:
    movd          m1, [r1+r2*1  ]
    movd          m3, [r1+r2*2  ]
    punpcklbw     m1, [r1+r2*1+1]
    punpcklbw     m3, [r1+r2*2+1]
    lea           r1, [r1+r2*2]
    movq          m2, m1
    movq          m4, m3
    pmaddubsw     m0, m7
    pmaddubsw     m1, m6
    pmaddubsw     m2, m7
    pmaddubsw     m3, m6
    paddw         m0, m5
    paddw         m2, m5
    paddw         m1, m0
    paddw         m3, m2
    psrlw         m1, 6
    movq          m0, m4
    psrlw         m3, 6
    packuswb      m1, m1
    packuswb      m3, m3
    CHROMAMC_AVG  m1, [r0  ]
    CHROMAMC_AVG  m3, [r0+r2]
    movd     [r0   ], m1
    movd     [r0+r2], m3
    sub          r3d, 2
    lea           r0, [r0+r2*2]
    jg .next2rows
    REP_RET
%endmacro

%define CHROMAMC_AVG NOTHING
INIT_XMM ssse3
chroma_mc8_ssse3_func put, h264, _rnd
chroma_mc8_ssse3_func put, vc1,  _nornd
INIT_MMX ssse3
chroma_mc4_ssse3_func put, h264

%define CHROMAMC_AVG DIRECT_AVG
INIT_XMM ssse3
chroma_mc8_ssse3_func avg, h264, _rnd
chroma_mc8_ssse3_func avg, vc1,  _nornd
INIT_MMX ssse3
chroma_mc4_ssse3_func avg, h264
