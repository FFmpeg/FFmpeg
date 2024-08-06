;******************************************************************************
;* software YUV to RGB converter
;*
;* Copyright (C) 2001-2007 Michael Niedermayer
;*           (c) 2010 Konstantin Shishkov
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

; below variables are named like mask_dwXY, which means to preserve dword No.X & No.Y
mask_dw036 : db -1, -1,  0,  0,  0,  0, -1, -1,  0,  0,  0,  0, -1, -1,  0,  0
mask_dw147 : db  0,  0, -1, -1,  0,  0,  0,  0, -1, -1,  0,  0,  0,  0, -1, -1
mask_dw25  : db  0,  0,  0,  0, -1, -1,  0,  0,  0,  0, -1, -1,  0,  0,  0,  0
rgb24_shuf1: db  0,  1,  6,  7, 12, 13,  2,  3,  8,  9, 14, 15,  4,  5, 10, 11
rgb24_shuf2: db 10, 11,  0,  1,  6,  7, 12, 13,  2,  3,  8,  9, 14, 15,  4,  5
rgb24_shuf3: db  4,  5, 10, 11,  0,  1,  6,  7, 12, 13,  2,  3,  8,  9, 14, 15
gbrp_shuf  : db  0,  8,  1,  9,  2, 10,  3, 11,  4, 12,  5, 13,  6, 14,  7, 15
pw_00ff: times 8 dw 255
pb_f8:   times 16 db 248
pb_e0:   times 16 db 224
pb_03:   times 16 db 3
pb_07:   times 16 db 7

SECTION .text

;-----------------------------------------------------------------------------
;
; YUV420/YUVA420 to RGB/BGR 15/16/24/32
; R = Y + ((vrCoff * (v - 128)) >> 8)
; G = Y - ((ugCoff * (u - 128) + vgCoff * (v - 128)) >> 8)
; B = Y + ((ubCoff * (u - 128)) >> 8)
;
;-----------------------------------------------------------------------------

%macro yuv2rgb_fn 3

%if %3 == 32
    %ifidn %1, yuva
    %define parameters index, image, pu_index, pv_index, pointer_c_dither, py_2index, pa_2index
    %define GPR_num 7
    %else
    %define parameters index, image, pu_index, pv_index, pointer_c_dither, py_2index
    %define GPR_num 6
    %endif
%else
    %ifidn %2, gbrp
    %define parameters index, image, dst_b, dst_r, pu_index, pv_index, pointer_c_dither, py_2index
    %define GPR_num 8
    %else
    %define parameters index, image, pu_index, pv_index, pointer_c_dither, py_2index
    %define GPR_num 6
    %endif
%endif

%define m_green m2
%define m_alpha m3
%define m_y m6
%define m_u m0
%define m_v m1
%ifidn %2, rgb
%define m_red m1
%define m_blue m0
%else
%define m_red m0
%define m_blue m1
%endif

%define time_num 2
%if ARCH_X86_32
%define reg_num 8
%define my_offset [pointer_c_ditherq + 8  * 8]
%define mu_offset [pointer_c_ditherq + 9  * 8]
%define mv_offset [pointer_c_ditherq + 10 * 8]
%define mug_coff  [pointer_c_ditherq + 7  * 8]
%define mvg_coff  [pointer_c_ditherq + 6  * 8]
%define my_coff   [pointer_c_ditherq + 3  * 8]
%define mub_coff  [pointer_c_ditherq + 5  * 8]
%define mvr_coff  [pointer_c_ditherq + 4  * 8]
%else ; ARCH_X86_64
%define reg_num 16
%define y_offset m8
%define u_offset m9
%define v_offset m10
%define ug_coff  m11
%define vg_coff  m12
%define y_coff   m13
%define ub_coff  m14
%define vr_coff  m15
%endif ; ARCH_X86_32/64

cglobal %1_420_%2%3, GPR_num, GPR_num, reg_num, parameters

%if ARCH_X86_64
    movsxd indexq, indexd
    VBROADCASTSD y_offset, [pointer_c_ditherq + 8  * 8]
    VBROADCASTSD u_offset, [pointer_c_ditherq + 9  * 8]
    VBROADCASTSD v_offset, [pointer_c_ditherq + 10 * 8]
    VBROADCASTSD ug_coff,  [pointer_c_ditherq + 7  * 8]
    VBROADCASTSD vg_coff,  [pointer_c_ditherq + 6  * 8]
    VBROADCASTSD y_coff,   [pointer_c_ditherq + 3  * 8]
    VBROADCASTSD ub_coff,  [pointer_c_ditherq + 5  * 8]
    VBROADCASTSD vr_coff,  [pointer_c_ditherq + 4  * 8]
%endif
.loop0:
    movu m_y, [py_2indexq + 2 * indexq]
    movh m_u, [pu_indexq  +     indexq]
    movh m_v, [pv_indexq  +     indexq]
    pxor m4, m4
    mova m7, m6
    punpcklbw m0, m4
    punpcklbw m1, m4
    mova m2, [pw_00ff]
    pand m6, m2
    psrlw m7, 8
    psllw m0, 3
    psllw m1, 3
    psllw m6, 3
    psllw m7, 3
%if ARCH_X86_32
    VBROADCASTSD m2, mu_offset
    VBROADCASTSD m3, mv_offset
    VBROADCASTSD m4, my_offset
    psubsw m0, m2 ; U = U - 128
    psubsw m1, m3 ; V = V - 128
    psubw  m6, m4
    psubw  m7, m4
    VBROADCASTSD m2, mug_coff
    VBROADCASTSD m3, mvg_coff
    VBROADCASTSD m4, my_coff
    VBROADCASTSD m5, mub_coff
    pmulhw m2, m0
    pmulhw m3, m1
    pmulhw m6, m4
    pmulhw m7, m4
    pmulhw m0, m5
    VBROADCASTSD m4, mvr_coff
    pmulhw m1, m4
%else ; ARCH_X86_64
    psubsw m0, u_offset ; U = U - 128
    psubsw m1, v_offset ; V = V - 128
    psubw  m6, y_offset
    psubw  m7, y_offset
    mova m2, m0
    mova m3, m1
    pmulhw m2, ug_coff
    pmulhw m3, vg_coff
    pmulhw m6, y_coff
    pmulhw m7, y_coff
    pmulhw m0, ub_coff
    pmulhw m1, vr_coff
%endif
    paddsw m2, m3
    mova m3, m7
    mova m5, m7
    paddsw m3, m0 ; B1 B3 B5 B7 ...
    paddsw m5, m1 ; R1 R3 R5 R7 ...
    paddsw m7, m2 ; G1 G3 G5 G7 ...
    paddsw m0, m6 ; B0 B2 B4 B6 ...
    paddsw m1, m6 ; R0 R2 R4 R6 ...
    paddsw m2, m6 ; G0 G2 G4 G6 ...

%if %3 == 24 ; PACK RGB24
    packuswb m0, m3 ; B0 B2 B4 B6 ... B1 B3 B5 B7 ...
    packuswb m1, m5 ; R0 R2 R4 R6 ... R1 R3 R5 R7 ...
    packuswb m2, m7 ; G0 G2 G4 G6 ... G1 G3 G5 G7 ...
%ifidn %2, gbrp ; PLANAR GBRP
%define depth 1
    mova   m4, [gbrp_shuf]
    pshufb m0, m4
    pshufb m1, m4
    pshufb m2, m4
    movu [imageq], m2
    movu [dst_bq], m0
    movu [dst_rq], m1
    add dst_bq, 8 * depth * time_num
    add dst_rq, 8 * depth * time_num
%else
%define depth 3
    mova m3, m_red
    mova m6, m_blue
    psrldq m_red, 8
    punpcklbw m3, m2     ; R0 G0 R2 G2 R4 G4 R6 G6 R8 G8 ...
    punpcklbw m6, m_red  ; B0 R1 B2 R3 B4 R5 B6 R7 B8 R9 ...
    punpckhbw m2, m_blue ; G1 B1 G3 B3 G5 B5 G7 B7 G9 B9 ...
    pshufb m3, [rgb24_shuf1] ; r0  g0  r6  g6  r12 g12 r2  g2  r8  g8  r14 g14 r4  g4  r10 g10
    pshufb m6, [rgb24_shuf2] ; b10 r11 b0  r1  b6  r7  b12 r13 b2  r3  b8  r9  b14 r15 b4  r5
    pshufb m2, [rgb24_shuf3] ; g5  b5  g11 b11 g1  b1  g7  b7  g13 b13 g3  b3  g9  b9  g15 b15
    mova   m7, [mask_dw036]
    mova   m4, [mask_dw147]
    mova   m5, [mask_dw25]
    pand   m0, m7, m3      ; r0  g0  --- --- --- --- r2  g2  --- --- --- --- r4  g4  --- ---
    pand   m1, m4, m6      ; --- --- b0  r1  --- --- --- --- b2  r3  --- --- --- --- b4  r5
    por    m0, m1
    pand   m1, m5, m2      ; --- --- --- --- g1  b1  --- --- --- --- g3  b3  --- --- --- ---
    por    m0, m1          ; r0  g0  b0  r1  g1  b1  r2  g2  b2  r3  g3  b3  r4  g4  b4  r5
    pand   m1, m7, m2      ; g5  b5  --- --- --- --- g7  b7  --- --- --- --- g9  b9  --- ---
    pand   m7, m6          ; b10 r11 --- --- --- --- b12 r13 --- --- --- --- b14 r15 --- ---
    pand   m6, m5          ; --- --- --- --- b6  r7  --- --- --- --- b8  r9  --- --- --- ---
    por    m1, m6
    pand   m6, m4, m3      ; --- --- r6  g6  --- --- --- --- r8  g8  --- --- --- --- r10 g10
    pand   m2, m4          ; --- --- g11 b11 --- --- --- --- g13 b13 --- --- --- --- g15 b15
    pand   m3, m5          ; --- --- --- --- r12 g12 --- --- --- --- r14 g14 --- --- --- ---
    por    m2, m7
    por    m1, m6          ; g5  b5  r6  g6  b6  r7  g7  b7  r8  g8  b8  r9  g9  b9  r10 g10
    por    m2, m3          ; b10 r11 g11 b11 r12 g12 b12 r13 g13 b13 r14 g14 b14 r15 g15 b15
    movu [imageq], m0
    movu [imageq + 16], m1
    movu [imageq + 32], m2
%endif ; PLANAR GBRP
%else ; PACK RGB15/16/32
    packuswb m0, m1
    packuswb m3, m5
    packuswb m2, m2
    mova m1, m0
    packuswb m7, m7
    punpcklbw m0, m3 ; B0 B1 B2 B3 ... B7
    punpckhbw m1, m3 ; R0 R1 R2 R3 ... R7
    punpcklbw m2, m7 ; G0 G1 G2 G3 ... G7
%if %3 == 32 ; PACK RGB32
%define depth 4
%ifidn %1, yuv
    pcmpeqd m3, m3 ; Set alpha empty
%else
    movu m3, [pa_2indexq + 2 * indexq] ; Load alpha
%endif
    mova m5, m_blue
    mova m6, m_red
    punpckhbw m5,     m_green
    punpcklbw m_blue, m_green
    punpckhbw m6,     m_alpha
    punpcklbw m_red,  m_alpha
    mova m_green, m_blue
    mova m_alpha, m5
    punpcklwd m_blue, m_red
    punpckhwd m_green, m_red
    punpcklwd m5, m6
    punpckhwd m_alpha, m6
    movu [imageq + 0], m_blue
    movu [imageq + 8  * time_num], m_green
    movu [imageq + 16 * time_num], m5
    movu [imageq + 24 * time_num], m_alpha
%else ; PACK RGB15/16
%define depth 2
    %define red_dither m3
    %define green_dither m4
    %define blue_dither m5
    VBROADCASTSD red_dither,   [pointer_c_ditherq + 0 * 8]
    VBROADCASTSD green_dither, [pointer_c_ditherq + 1 * 8]
    VBROADCASTSD blue_dither,  [pointer_c_ditherq + 2 * 8]
%if %3 == 15
%define gmask pb_03
%define isRGB15 1
%else
%define gmask pb_07
%define isRGB15 0
%endif
    paddusb m0, blue_dither
    paddusb m2, green_dither
    paddusb m1, red_dither
    pand m0, [pb_f8]
    pand m1, [pb_f8]
    mova m3, m2
    psllw m2, 3 - isRGB15
    psrlw m3, 5 + isRGB15
    psrlw m0, 3
    psrlw m1, isRGB15
    pand m2, [pb_e0]
    pand m3, [gmask]
    por m0, m2
    por m1, m3
    mova m2, m0
    punpcklbw m0, m1
    punpckhbw m2, m1
    movu [imageq], m0
    movu [imageq + 8 * time_num], m2
%endif ; PACK RGB15/16
%endif ; PACK RGB15/16/32

add imageq, 8 * depth * time_num
add indexq, 4 * time_num
js .loop0

RET

%endmacro

INIT_XMM ssse3
yuv2rgb_fn yuv,  rgb, 24
yuv2rgb_fn yuv,  bgr, 24
yuv2rgb_fn yuv,  rgb, 32
yuv2rgb_fn yuv,  bgr, 32
yuv2rgb_fn yuva, rgb, 32
yuv2rgb_fn yuva, bgr, 32
yuv2rgb_fn yuv,  rgb, 15
yuv2rgb_fn yuv,  rgb, 16
%if ARCH_X86_64
yuv2rgb_fn yuv,  gbrp, 24
%endif
