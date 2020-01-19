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

pw_00ff: times 4 dw 255
pb_f8:   times 8 db 248
pb_e0:   times 8 db 224
pb_03:   times 8 db 3
pb_07:   times 8 db 7

mask_1101: dw -1, -1,  0, -1
mask_0010: dw  0,  0, -1,  0
mask_0110: dw  0, -1, -1,  0
mask_1001: dw -1,  0,  0, -1
mask_0100: dw  0, -1,  0,  0

SECTION .text

;-----------------------------------------------------------------------------
;
; YUV420/YUVA420 to RGB/BGR 15/16/24/32
; R = Y + ((vrCoff * (v - 128)) >> 8)
; G = Y - ((ugCoff * (u - 128) + vgCoff * (v - 128)) >> 8)
; B = Y + ((ubCoff * (u - 128)) >> 8)
;
;-----------------------------------------------------------------------------

%macro MOV_H2L 1
psrlq %1, 32
%endmacro

%macro yuv2rgb_fn 3

%if %3 == 32
    %ifidn %1, yuva
    %define parameters index, image, pu_index, pv_index, pointer_c_dither, py_2index, pa_2index
    %define GPR_num 7
    %endif
%else
    %define parameters index, image, pu_index, pv_index, pointer_c_dither, py_2index
    %define GPR_num 6
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

%define time_num 1
%define reg_num 8
%define y_offset [pointer_c_ditherq + 8  * 8]
%define u_offset [pointer_c_ditherq + 9  * 8]
%define v_offset [pointer_c_ditherq + 10 * 8]
%define ug_coff  [pointer_c_ditherq + 7  * 8]
%define vg_coff  [pointer_c_ditherq + 6  * 8]
%define y_coff   [pointer_c_ditherq + 3  * 8]
%define ub_coff  [pointer_c_ditherq + 5  * 8]
%define vr_coff  [pointer_c_ditherq + 4  * 8]

cglobal %1_420_%2%3, GPR_num, GPR_num, reg_num, parameters

%if ARCH_X86_64
    movsxd indexq, indexd
%endif
    mova m_y, [py_2indexq + 2 * indexq]
    movh m_u, [pu_indexq  +     indexq]
    movh m_v, [pv_indexq  +     indexq]
.loop0:
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
    psubsw m0, u_offset ; U = U - 128
    psubsw m1, v_offset ; V = V - 128
    psubw m6, y_offset
    psubw m7, y_offset
    mova m2, m0
    mova m3, m1
    pmulhw m2, ug_coff
    pmulhw m3, vg_coff
    pmulhw m6, y_coff
    pmulhw m7, y_coff
    pmulhw m0, ub_coff
    pmulhw m1, vr_coff
    paddsw m2, m3
    mova m3, m7
    mova m5, m7
    paddsw m3, m0 ; B1 B3 B5 B7 ...
    paddsw m5, m1 ; R1 R3 R5 R7 ...
    paddsw m7, m2 ; G1 G3 G4 G7 ...
    paddsw m0, m6 ; B0 B2 B4 B6 ...
    paddsw m1, m6 ; R0 R2 R4 R6 ...
    paddsw m2, m6 ; G0 G2 G4 G6 ...

%if %3 == 24 ; PACK RGB24
%define depth 3
    packuswb m0, m3 ; R0 R2 R4 R6 ... R1 R3 R5 R7 ...
    packuswb m1, m5 ; B0 B2 B4 B6 ... B1 B3 B5 B7 ...
    packuswb m2, m7 ; G0 G2 G4 G6 ... G1 G3 G5 G7 ...
    mova m3, m_red
    mova m6, m_blue
    MOV_H2L m_red
    punpcklbw m3, m2     ; R0 G0 R2 G2 R4 G4 R6 G6 R8 G8 ...
    punpcklbw m6, m_red  ; B0 R1 B2 R3 B4 R5 B6 R7 B8 R9 ...
    mova m5, m3
    punpckhbw m2, m_blue ; G1 B1 G3 B3 G5 B5 G7 B7 G9 B9 ...
    punpcklwd m3 ,m6     ; R0 G0 B0 R1 R2 G2 B2 R3
    punpckhwd m5, m6     ; R4 G4 B4 R5 R6 G6 B6 R7
%if cpuflag(mmxext)
    pshufw m1, m2, 0xc6
    pshufw m6, m3, 0x84
    pshufw m7, m5, 0x38
    pand m6, [mask_1101] ; R0 G0 B0 R1 -- -- R2 G2
    movq m0, m1
    pand m7, [mask_0110] ; -- -- R6 G6 B6 R7 -- --
    movq m2, m1
    pand m1, [mask_0100] ; -- -- G3 B3 -- -- -- --
    psrlq m3, 48         ; B2 R3 -- -- -- -- -- --
    pand m0, [mask_0010] ; -- -- -- -- G1 B1 -- --
    psllq m5, 32         ; -- -- -- -- R4 G4 B4 R5
    pand m2, [mask_1001] ; G5 B5 -- -- -- -- G7 B7
    por m1, m3
    por m0, m6
    por m1, m5
    por m2, m7
    movntq [imageq], m0
    movntq [imageq + 8], m1
    movntq [imageq + 16], m2
%else ; cpuflag(mmx)
    movd [imageq], m3      ; R0 G0 R2 G2
    movd [imageq + 4], m2  ; G1 B1
    psrlq m3, 32
    psrlq m2, 16
    movd [imageq + 6], m3  ; R2 G2 B2 R3
    movd [imageq + 10], m2 ; G3 B3
    psrlq m2, 16
    movd [imageq + 12], m5 ; R4 G4 B4 R5
    movd [imageq + 16], m2 ; G5 B5
    psrlq m5, 32
    movd [imageq + 20], m2 ; -- -- G7 B7
    movd [imageq + 18], m5 ; R6 G6 B6 R7
%endif
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
    mova m3, [pa_2indexq + 2 * indexq] ; Load alpha
%endif
    mova m5, m_blue
    mova m6, m_red
    punpckhbw m5, m_green
    punpcklbw m_blue, m_green
    punpckhbw m6, m_alpha
    punpcklbw m_red, m_alpha
    mova m_green, m_blue
    mova m_alpha, m5
    punpcklwd m_blue, m_red
    punpckhwd m_green, m_red
    punpcklwd m5, m6
    punpckhwd m_alpha, m6
    mova [imageq + 0], m_blue
    mova [imageq + 8 * time_num], m_green
    mova [imageq + 16 * time_num], m5
    mova [imageq + 24 * time_num], m_alpha
%else ; PACK RGB15/16
%define depth 2
%define blue_dither  [pointer_c_ditherq + 2 * 8]
%define green_dither [pointer_c_ditherq + 1 * 8]
%define red_dither   [pointer_c_ditherq + 0 * 8]
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
    mova [imageq], m0
    mova [imageq + 8 * time_num], m2
%endif ; PACK RGB15/16
%endif ; PACK RGB15/16/32

mova m_y, [py_2indexq + 2 * indexq + 8 * time_num]
movh m_v, [pv_indexq  +     indexq + 4 * time_num]
movh m_u, [pu_indexq  +     indexq + 4 * time_num]
add imageq, 8 * depth * time_num
add indexq, 4 * time_num
js .loop0

REP_RET

%endmacro

INIT_MMX mmx
yuv2rgb_fn yuv,  rgb, 24
yuv2rgb_fn yuv,  bgr, 24
yuv2rgb_fn yuv,  rgb, 32
yuv2rgb_fn yuv,  bgr, 32
yuv2rgb_fn yuva, rgb, 32
yuv2rgb_fn yuva, bgr, 32
yuv2rgb_fn yuv,  rgb, 15
yuv2rgb_fn yuv,  rgb, 16

INIT_MMX mmxext
yuv2rgb_fn yuv, rgb, 24
yuv2rgb_fn yuv, bgr, 24
