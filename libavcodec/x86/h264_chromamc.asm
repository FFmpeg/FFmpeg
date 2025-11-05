;******************************************************************************
;* MMX/SSSE3-optimized functions for H.264 chroma MC
;* Copyright (c) 2005 Zoltan Hidvegi <hzoli -a- hzoli -d- com>,
;*               2005-2008 Loren Merritt
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

%include "config_components.asm"
%include "libavutil/x86/x86util.asm"

SECTION_RODATA

cextern pw_3
cextern pw_4
cextern pw_8
pw_28: times 8 dw 28
cextern pw_32
cextern pw_64

cextern rv40_bias

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

%macro chroma_mc2_mmx_func 2
cglobal %1_%2_chroma_mc2, 6, 7, 0
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
    RET
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


INIT_MMX mmxext
%define CHROMAMC_AVG  NOTHING
%define CHROMAMC_AVG4 NOTHING
chroma_mc2_mmx_func put, h264

%define CHROMAMC_AVG  DIRECT_AVG
%define CHROMAMC_AVG4 COPY_AVG
chroma_mc2_mmx_func avg, h264

%macro chroma_mc8_ssse3_func 2-3
cglobal %1_%2_chroma_mc8%3, 6, 7+UNIX64, 8
    mov          r6d, r5d
    or           r6d, r4d
    jne .at_least_one_non_zero
    ; mx == 0 AND my == 0 - no filter needed
..@%1_%2_chroma_mc8_no_filter_ %+ cpuname:
    mv0_pixels_mc8
    RET

.at_least_one_non_zero:
    test         r5d, r5d
    je .my_is_zero
    test         r4d, r4d
    je .mx_is_zero

    ; general case, bilinear
    movdqa        m5, [rnd_2d_%2]
..@%1_%2_chroma_mc8_both_nonzero_ %+ cpuname:
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
    RET

.my_is_zero:
    movdqa        m6, [rnd_1d_%2]
..@%1_%2_chroma_mc8_my_zero_ %+ cpuname:
    mov          r5d, r4d
    shl          r4d, 8
    add           r4, 8
    sub           r4, r5          ; 255*x+8 = x<<8 | (8-x)
    movd          m7, r4d
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
    RET

.mx_is_zero:
    movdqa        m6, [rnd_1d_%2]
..@%1_%2_chroma_mc8_mx_zero_ %+ cpuname:
    mov          r4d, r5d
    shl          r5d, 8
    add           r5, 8
    sub           r5, r4          ; 255*y+8 = y<<8 | (8-y)
    movd          m7, r5d
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
    RET
%endmacro

%macro chroma_mc4_ssse3_func 2
cglobal %1_%2_chroma_mc4, 6, 7+UNIX64, 8
    mova          m5, [pw_32]
..@%1_%2_chroma_mc4_after_init_ %+ cpuname:
    mov          r6d, r4d
    shl          r4d, 8
    movd          m0, [r1]
    sub          r6d, 8
    sub          r4d, r6d         ; x << 8 | (8-x)
    mov          r6d, r5d
    shl          r5d, 16
    movd          m1, [r1+1]
    sub          r6d, 8
    sub          r5d, r6d         ; y << 16 | (8-y)
    imul         r4d, r5d         ; xy << 24 | (8-x)y << 16 | x(8-y) << 8 | (8-x)(8-y)
    add           r1, r2

    movd          m6, r4d         ; ABCD
    punpcklwd     m6, m6          ; ABABCDCD
    pshufd        m7, m6, 0x55    ; CDCDCDCDCDCDCDCD
    punpcklbw     m0, m1
    pshufd        m6, m6, 0x0     ; ABABABABABABABAB

.next2rows:
    movd          m1, [r1]
    movd          m2, [r1+1]
    movd          m3, [r1+r2]
    movd          m4, [r1+r2+1]
    punpcklbw     m1, m2
    punpcklqdq    m0, m1
    pmaddubsw     m0, m6
    punpcklbw     m3, m4
    punpcklqdq    m1, m3
    pmaddubsw     m1, m7
%ifidn %1, avg
    movd          m2, [r0]
    movd          m4, [r0+r2]
%endif
    paddw         m0, m5
    lea           r1, [r1+r2*2]
    paddw         m0, m1
    psrlw         m0, 6
    packuswb      m0, m0
    pshufd        m1, m0, 0x1
%ifidn %1, avg
    pavgb         m0, m2
    pavgb         m1, m4
%endif
    sub          r3d, 2
    movd        [r0], m0
    movd     [r0+r2], m1
    mova          m0, m3
    lea           r0, [r0+r2*2]
    jg .next2rows
    RET
%endmacro

%macro rv40_get_bias 1 ; dst reg
%if !PIC || UNIX64
    ; on UNIX64 we have enough volatile registers
%if PIC && UNIX64
    lea           r7, [rv40_bias]
%endif
    mov          r6d, r5d
    and          r6d, 6         ; &~1 for mx/my=[0,7]
    lea          r6d, [r6d*4+r4d]
    sar          r6d, 1
%if PIC && UNIX64
    movd          %1, [r7+4*r6]
%else
    movd          %1, [rv40_bias+4*r6]
%endif
%else  ; PIC && !UNIX64, de facto WIN64
    lea           r6, [rv40_bias]
%ifidn r5d, r5m ; always false for currently supported calling conventions
    push          r5
%endif
    and          r5d, 6         ; &~1 for mx/my=[0,7]
    lea          r5d, [r5d*4+r4d]
    sar          r5d, 1
    movd          %1, [r6+4*r5]
%ifidn r5d, r5m
    pop           r5
%else
    mov          r5d, r5m
%endif
%endif
    SPLATW        %1, %1
%endmacro

%macro rv40_chroma_mc8_func 1 ; put vs avg
%if CONFIG_RV40_DECODER
    cglobal rv40_%1_chroma_mc8, 6, 7+UNIX64, 8
    mov          r6d, r5d
    or           r6d, r4d
    jz           ..@%1_h264_chroma_mc8_no_filter_ %+ cpuname
    rv40_get_bias m5
    ; the bilinear code expects bias in m5, the one-dimensional code in m6
    mova          m6, m5
    psraw         m6, 3
    test         r5d, r5d
    je           ..@%1_h264_chroma_mc8_my_zero_ %+ cpuname
    test         r4d, r4d
    je           ..@%1_h264_chroma_mc8_mx_zero_ %+ cpuname
    jmp          ..@%1_h264_chroma_mc8_both_nonzero_ %+ cpuname
%endif
%endmacro

%macro rv40_chroma_mc4_func 1 ; put vs avg
%if CONFIG_RV40_DECODER
    cglobal rv40_%1_chroma_mc4, 6, 7+UNIX64, 8
    rv40_get_bias m5
    jmp           ..@%1_h264_chroma_mc4_after_init_ %+ cpuname
%endif
%endmacro

INIT_XMM ssse3
%define CHROMAMC_AVG NOTHING
chroma_mc8_ssse3_func put, h264, _rnd
chroma_mc8_ssse3_func put, vc1,  _nornd
rv40_chroma_mc8_func put
chroma_mc4_ssse3_func put, h264
rv40_chroma_mc4_func put

%define CHROMAMC_AVG DIRECT_AVG
chroma_mc8_ssse3_func avg, h264, _rnd
chroma_mc8_ssse3_func avg, vc1,  _nornd
rv40_chroma_mc8_func avg
chroma_mc4_ssse3_func avg, h264
rv40_chroma_mc4_func avg
