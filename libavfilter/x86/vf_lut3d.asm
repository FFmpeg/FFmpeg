;*****************************************************************************
;* x86-optimized functions for lut3d filter
;*
;* Copyright (c) 2021 Mark Reid <mindmark@gmail.com>
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
pd_1f:  times 8 dd 1.0
pd_3f:  times 8 dd 3.0
pd_65535f:     times 8 dd 65535.0
pd_65535_invf: times 8 dd 0x37800080 ;1.0/65535.0

pb_shuffle16:         db    0,    1, 0x80, 0x80, \
                            2,    3, 0x80, 0x80, \
                            4,    5, 0x80, 0x80, \
                            6,    7, 0x80, 0x80

pb_lo_pack_shuffle16: db    0,    1,    4,    5, \
                            8,    9,   12,   13, \
                         0x80, 0x80, 0x80, 0x80, \
                         0x80, 0x80, 0x80, 0x80

pb_hi_pack_shuffle16: db 0x80, 0x80, 0x80, 0x80, \
                         0x80, 0x80, 0x80, 0x80, \
                            0,    1,    4,    5, \
                            8,    9,   12,   13

SECTION .text

struc Lut3DPreLut
    .size:    resd 1
    .min:     resd 3
    .max:     resd 3
    .scale:   resd 3
    .lut:     resq 3
endstruc

struc LUT3DContext
    .class:        resq 1
    .lut:          resq 1
    .lutsize:      resd 1
    .lutsize2:     resd 1
    .scale:        resd 3
endstruc

%define AV_NUM_DATA_POINTERS 8

struc AVFrame
    .data:          resq AV_NUM_DATA_POINTERS
    .linesize:      resd AV_NUM_DATA_POINTERS
    .extended_data: resq 1
    .width:         resd 1
    .height:        resd 1
endstruc

%define rm   rsp
%define gm   rsp+mmsize
%define bm   rsp+(mmsize*2)

%define lut3dsizem  [rsp+mmsize*3]
%define lut3dsize2m [rsp+mmsize*4]
%define lut3dmaxm   [rsp+mmsize*5]
%define prelutmaxm  [rsp+mmsize*6]

%define scalerm [rsp+mmsize*7]
%define scalegm [rsp+mmsize*8]
%define scalebm [rsp+mmsize*9]

%define prelutminrm [rsp+mmsize*10]
%define prelutmingm [rsp+mmsize*11]
%define prelutminbm [rsp+mmsize*12]

%define prelutscalerm [rsp+mmsize*13]
%define prelutscalegm [rsp+mmsize*14]
%define prelutscalebm [rsp+mmsize*15]

; data pointers
%define srcrm [rsp+mmsize*16 +  0]
%define srcgm [rsp+mmsize*16 +  8]
%define srcbm [rsp+mmsize*16 + 16]
%define srcam [rsp+mmsize*16 + 24]

%define dstrm [rsp+mmsize*16 + 32]
%define dstgm [rsp+mmsize*16 + 40]
%define dstbm [rsp+mmsize*16 + 48]
%define dstam [rsp+mmsize*16 + 56]

; 1 - prev
; 2 - next
; 3 - offset
%macro FETCH_PRELUT_PN 3
    mov tmp2d, [rm + %3]
    mov tmp3d, [gm + %3]
    movss xm%1, [tmpq + tmp2q*4]
    movss xm%2, [tmpq + tmp3q*4]
    movss [rm + %3], xm%1
    movss [gm + %3], xm%2
%endmacro

; 1 - p
; 2 - n
; 3 - p indices
; 4 - n indices
%macro GATHER_PRELUT 4
    %if cpuflag(avx2)
        vpcmpeqb m7, m7
        vgatherdps m%1, [tmpq + m%3*4], m7 ; p
        vpcmpeqb m9, m9
        vgatherdps m%2, [tmpq + m%4*4], m9 ; n
    %else
        mova [rm], m%3
        mova [gm], m%4
        FETCH_PRELUT_PN %1, %2, 0
        FETCH_PRELUT_PN %1, %2, 4
        FETCH_PRELUT_PN %1, %2, 8
        FETCH_PRELUT_PN %1, %2, 12
    %if mmsize > 16
        FETCH_PRELUT_PN %1, %2, 16
        FETCH_PRELUT_PN %1, %2, 20
        FETCH_PRELUT_PN %1, %2, 24
        FETCH_PRELUT_PN %1, %2, 28
    %endif
        movu m%1, [rm]
        movu m%2, [gm]
    %endif
%endmacro

%macro FLOORPS 2
    %if mmsize > 16
        vroundps %1, %2, 0x01
    %else
        cvttps2dq %1, %2
        cvtdq2ps  %1, %1
    %endif
%endmacro

; %1 = %2 * %3 + %1
%macro MADD3 3
%if cpuflag(avx2)
    vfmadd231ps %1, %2, %3
%else
    mulps %2, %2, %3
    addps %1, %1, %2
%endif
%endmacro

; 1 - dst
; 2 - index
; 3 - min
; 4 - scale
; assumes lut max m13, m14 1.0f, zero m15
%macro APPLY_PRELUT 4
    ; scale
    subps m5, m%1, %3 ; v - min
    mulps m5, m5, %4  ; v * scale
    ; clamp
    maxps m5, m5, m15 ; max zero, Max first, NAN set to zero
    minps m5, m5, m13 ; min lut max

    FLOORPS m3, m5    ; prev index
    subps m5, m5, m3  ; d
    addps m4, m3, m14 ; p+1 = n index
    minps m4, m4, m13 ; clamp n idex

    mov tmpq, [prelutq + Lut3DPreLut.lut + %2*8]
    cvttps2dq m6, m3
    cvttps2dq m10, m4
    GATHER_PRELUT %1, 4, 6, 10

    ; lerp
    subps m8, m4, m%1
    MADD3 m%1, m8, m5

%endmacro

; 1 - dst
; 2 - scale
; assumes lut max m13, zero m15
%macro APPLY_SCALE 2
   mulps m%1, m%1, %2
   maxps m%1, m%1, m15 ; Max first, NAN set to zero
   minps m%1, m%1, m13
%endmacro

%macro BLEND 4
%if mmsize > 16
    vblendvps %1, %2, %3, %4
%else
    %ifidni %1,%2
        %error operand 1 must not equal operand 2
    %endif
    %ifidni %1,%3
        %error operand 1 must not equal operand 3
    %endif
    mova  %1, %2
    xorps %1, %3
    andps %1, %4
    xorps %1, %2
%endif
%endmacro

%macro ADD3 4
    addps %1, %2, %3
    addps %1, %1, %4
%endmacro

%macro FETCH_LUT3D_RGB 4
    mov tmp2d, [rm + %4]
    movss xm%1, [tmpq + tmp2q*4 + 0]
    movss xm%2, [tmpq + tmp2q*4 + 4]
    movss xm%3, [tmpq + tmp2q*4 + 8]
    movss [rm + %4], xm%1
    movss [gm + %4], xm%2
    movss [bm + %4], xm%3
%endmacro

; 1 - dstr
; 2 - dstg
; 3 - dstb
; 4 - indices
%macro GATHER_LUT3D_INDICES 4
%if cpuflag(avx2)
    vpcmpeqb m3, m3
    vgatherdps m%1, [tmpq + m%4*4 + 0], m3
    vpcmpeqb m14, m14
    vgatherdps m%2, [tmpq + m%4*4 + 4], m14
    vpcmpeqb m15, m15
    vgatherdps m%3, [tmpq + m%4*4 + 8], m15
%else
    movu [rm], m%4
    FETCH_LUT3D_RGB %1, %2, %3, 0
    FETCH_LUT3D_RGB %1, %2, %3, 4
    FETCH_LUT3D_RGB %1, %2, %3, 8
    FETCH_LUT3D_RGB %1, %2, %3, 12
%if mmsize > 16
    FETCH_LUT3D_RGB %1, %2, %3, 16
    FETCH_LUT3D_RGB %1, %2, %3, 20
    FETCH_LUT3D_RGB %1, %2, %3, 24
    FETCH_LUT3D_RGB %1, %2, %3, 28
%endif
    movu m%1, [rm]
    movu m%2, [gm]
    movu m%3, [bm]
%endif
%endmacro

%macro interp_tetrahedral 0
    %define d_r m0
    %define d_g m1
    %define d_b m2

    %define prev_r m3
    %define prev_g m4
    %define prev_b m5

    %define next_r m6
    %define next_g m7
    %define next_b m8

    %define x0 m4
    %define x1 m5
    %define x2 m6

    ; setup prev index
    FLOORPS prev_r, m0
    FLOORPS prev_g, m1
    FLOORPS prev_b, m2

    ; setup deltas
    subps d_r, m0, prev_r
    subps d_g, m1, prev_g
    subps d_b, m2, prev_b

    ; setup next index
    addps next_r, prev_r, m14 ; +1
    minps next_r, next_r, m13 ; clamp lutmax

    addps next_g, prev_g, m14 ; +1
    minps next_g, next_g, m13 ; clamp lutmax

    addps next_b, prev_b, m14 ; +1
    minps next_b, next_b, m13 ; clamp lutmax

    ; prescale indices
    mulps prev_r, prev_r, lut3dsize2m
    mulps next_r, next_r, lut3dsize2m

    mulps prev_g, prev_g, lut3dsizem
    mulps next_g, next_g, lut3dsizem

    mulps prev_b, prev_b, [pd_3f]
    mulps next_b, next_b, [pd_3f]

    ; cxxxa m10
    ; 1 is the delta that is the largest
    ; r> == c100 == (r>g && r>b)
    ; g> == c010 == (g>r && g>b)
    ; b> == c001 == (b>r && b>g)
    ; if delta > other 2 use next else prev

    ; cxxxb m11;
    ; 0 is the delta that is the smallest
    ; r< == c011 == (r<=g && r<=b)
    ; g< == c101 == (g<=r && g<=b)
    ; b< == c110 == (b<=r && b<=g)
    ; if delta <= other 2 use prev else next

    cmpps m13, d_r, d_g, 0x1E ;  r>g
    cmpps m14, d_g, d_b, 0x1E ;  g>b
    cmpps m15, d_b, d_r, 0x1E ;  b>r

    ; r> !b>r && r>g
    andnps m9, m15, m13
    BLEND m10, prev_r, next_r, m9

    ; r< !r>g && b>r
    andnps m9, m13, m15
    BLEND m11, next_r, prev_r, m9

    ; g> !r>g && g>b
    andnps m9, m13, m14
    BLEND m12, prev_g, next_g, m9
    addps m10, m10, m12

    ; g< !g>b && r>g
    andnps m9, m14, m13
    BLEND m12, next_g, prev_g, m9
    addps m11, m11, m12

    ; b> !g>b && b>r
    andnps m9, m14, m15
    BLEND m12, prev_b, next_b, m9
    addps m10, m10, m12

    ; b< !b>r && g>b
    andnps m9, m15, m14
    BLEND m12, next_b, prev_b, m9
    addps m11, m11, m12

    ; c000 m12;
    ADD3 m12, prev_r, prev_g, prev_b

    ; c111 m13;
    ADD3 m13, next_r, next_g, next_b

    ; sort delta r,g,b x0 >= x1 >= x2
    minps m7, d_r, d_g
    maxps m8, d_r, d_g

    minps x2, m7, d_b
    maxps m7, m7, d_b

    maxps x0, m8, d_b
    minps x1, m8, m7

    ; convert indices to integer
    cvttps2dq m12, m12
    cvttps2dq m10, m10
    cvttps2dq m11, m11
    cvttps2dq m13, m13

    ; now the gathering festival
    mov tmpq, [ctxq + LUT3DContext.lut]

    GATHER_LUT3D_INDICES 0, 1, 2, 12
    movu m14, [pd_1f]
    subps m14, m14, x0; 1 - x0

    mulps m0, m0, m14
    mulps m1, m1, m14
    mulps m2, m2, m14

    GATHER_LUT3D_INDICES 7, 8, 9, 10
    subps m14, x0, x1; x0 - x1
    MADD3 m0, m7, m14
    MADD3 m1, m8, m14
    MADD3 m2, m9, m14

    GATHER_LUT3D_INDICES 7, 8, 9, 11
    subps m14, x1, x2; x1 - x2
    MADD3 m0, m7, m14
    MADD3 m1, m8, m14
    MADD3 m2, m9, m14

    GATHER_LUT3D_INDICES 7, 8, 9, 13
    MADD3 m0, m7, x2
    MADD3 m1, m8, x2
    MADD3 m2, m9, x2

%endmacro

%macro INIT_DATA_PTR 3
    mov ptrq, [%2 + AVFrame.data     + %3 * 8]
    mov tmpd, [%2 + AVFrame.linesize + %3 * 4]
    imul tmpd, slice_startd
    add ptrq, tmpq
    mov %1, ptrq
%endmacro

%macro INC_DATA_PTR 3
    mov tmpd, [%2 + AVFrame.linesize + %3 * 4]
    mov ptrq, %1
    add ptrq, tmpq
    mov %1, ptrq
%endmacro

%macro LOAD16 2
    mov ptrq, %2
    %if mmsize > 16
        movu xm%1, [ptrq + xq*2]
    %else
        movsd xm%1, [ptrq + xq*2]
    %endif
    %if cpuflag(avx2)
        vpmovzxwd m%1, xm%1
    %else
        %if mmsize > 16
            pshufd xm4, xm%1, (1 << 6 | 0 << 4 | 3 << 2 | 2 << 0)
            pshufb xm%1, xm6 ; pb_shuffle16
            pshufb xm4,  xm6 ; pb_shuffle16
            vinsertf128 m%1, m%1, xm4, 1
        %else
            pshufd  xm%1, xm%1, (3 << 6 | 1 << 4 | 3 << 2 | 0 << 0)
            pshuflw xm%1, xm%1, (2 << 6 | 1 << 4 | 2 << 2 | 0 << 0)
            pshufhw xm%1, xm%1, (2 << 6 | 1 << 4 | 2 << 2 | 0 << 0)
        %endif
    %endif
    cvtdq2ps m%1, m%1
    mulps m%1, m%1, m7 ; pd_65535_invf
%endmacro

%macro STORE16 2
    mulps m%2, m%2, m5  ; [pd_65535f]
    minps m%2, m%2, m5  ; [pd_65535f]
    maxps m%2, m%2, m15 ; zero
    cvttps2dq m%2, m%2
    %if mmsize > 16
        vextractf128 xm4, m%2, 1
        pshufb xm%2, xm6 ; [pb_lo_pack_shuffle16]
        pshufb xm4,  xm7 ; [pb_hi_pack_shuffle16]
        por xm%2, xm4
    %else
        pshuflw xm%2, xm%2, (1 << 6 | 1 << 4 | 2 << 2 | 0 << 0)
        pshufhw xm%2, xm%2, (1 << 6 | 1 << 4 | 2 << 2 | 0 << 0)
        pshufd  xm%2, xm%2, (3 << 6 | 3 << 4 | 2 << 2 | 0 << 0)
    %endif
    mov ptrq, %1
    %if mmsize > 16
        movu [ptrq + xq*2], xm%2
    %else
        movsd [ptrq + xq*2], xm%2
    %endif
%endmacro

; 1 - interp method
; 2 - format_name
; 3 - depth
; 4 - is float format
%macro DEFINE_INTERP_FUNC 4
cglobal interp_%1_%2, 7, 13, 16, mmsize*16+(8*8), ctx, prelut, src_image, dst_image, slice_start, slice_end, has_alpha, width, x, ptr, tmp, tmp2, tmp3
    ; store lut max and lutsize
    mov tmpd, dword [ctxq + LUT3DContext.lutsize]
    cvtsi2ss xm0, tmpd
    mulss xm0, xm0, [pd_3f]
    VBROADCASTSS m0, xm0
    mova lut3dsizem, m0
    sub tmpd, 1
    cvtsi2ss xm0, tmpd
    VBROADCASTSS m0, xm0
    mova lut3dmaxm, m0

    ; scale_r
    mulss xm1, xm0, dword [ctxq + LUT3DContext.scale + 0*4]
    VBROADCASTSS m1, xm1
    mova scalerm, m1

    ; scale_g
    mulss xm1, xm0, dword [ctxq + LUT3DContext.scale + 1*4]
    VBROADCASTSS m1, xm1
    mova scalegm, m1

    ; scale_b
    mulss xm1, xm0, dword [ctxq + LUT3DContext.scale + 2*4]
    VBROADCASTSS m1, xm1
    mova scalebm, m1

    ; store lutsize2
    cvtsi2ss xm0, dword [ctxq + LUT3DContext.lutsize2]
    mulss xm0, xm0, [pd_3f]
    VBROADCASTSS m0, xm0
    mova lut3dsize2m, m0

    ; init prelut values
    cmp prelutq, 0
    je %%skip_init_prelut
        mov tmpd, dword [prelutq + Lut3DPreLut.size]
        sub tmpd, 1
        cvtsi2ss xm0, tmpd
        VBROADCASTSS m0, xm0
        mova prelutmaxm, m0
        VBROADCASTSS m0, dword [prelutq + Lut3DPreLut.min + 0*4]
        mova prelutminrm, m0
        VBROADCASTSS m0, dword [prelutq + Lut3DPreLut.min + 1*4]
        mova prelutmingm, m0
        VBROADCASTSS m0, dword [prelutq + Lut3DPreLut.min + 2*4]
        mova prelutminbm, m0
        VBROADCASTSS m0, dword [prelutq + Lut3DPreLut.scale + 0*4]
        mova prelutscalerm, m0
        VBROADCASTSS m0, dword [prelutq + Lut3DPreLut.scale + 1*4]
        mova prelutscalegm, m0
        VBROADCASTSS m0, dword [prelutq + Lut3DPreLut.scale + 2*4]
        mova prelutscalebm, m0
    %%skip_init_prelut:

    mov widthd,  [src_imageq + AVFrame.width]

    ; gbra pixel order
    INIT_DATA_PTR srcrm, src_imageq, 2
    INIT_DATA_PTR srcgm, src_imageq, 0
    INIT_DATA_PTR srcbm, src_imageq, 1
    INIT_DATA_PTR srcam, src_imageq, 3

    INIT_DATA_PTR dstrm, dst_imageq, 2
    INIT_DATA_PTR dstgm, dst_imageq, 0
    INIT_DATA_PTR dstbm, dst_imageq, 1
    INIT_DATA_PTR dstam, dst_imageq, 3

    %%loop_y:
        xor xq, xq
        %%loop_x:
            movu m14, [pd_1f]
            xorps m15, m15, m15
            %if %4 ; float
                mov ptrq, srcrm
                movu m0, [ptrq + xq*4]
                mov ptrq, srcgm
                movu m1, [ptrq + xq*4]
                mov ptrq, srcbm
                movu m2, [ptrq + xq*4]
            %else
                ; constants for LOAD16
                movu m7, [pd_65535_invf]
                %if notcpuflag(avx2) && mmsize >= 32
                    movu xm6, [pb_shuffle16]
                %endif
                LOAD16 0, srcrm
                LOAD16 1, srcgm
                LOAD16 2, srcbm
            %endif

            cmp prelutq, 0
            je %%skip_prelut
                mova m13, prelutmaxm
                APPLY_PRELUT 0, 0, prelutminrm, prelutscalerm
                APPLY_PRELUT 1, 1, prelutmingm, prelutscalegm
                APPLY_PRELUT 2, 2, prelutminbm, prelutscalebm
            %%skip_prelut:

            mova m13, lut3dmaxm
            APPLY_SCALE 0, scalerm
            APPLY_SCALE 1, scalegm
            APPLY_SCALE 2, scalebm

            interp_%1

            %if %4 ; float
                mov ptrq, dstrm
                movu [ptrq + xq*4], m0
                mov ptrq, dstgm
                movu [ptrq + xq*4], m1
                mov ptrq, dstbm
                movu [ptrq + xq*4], m2
                cmp has_alphad, 0
                je %%skip_alphaf
                    mov ptrq, srcam
                    movu m0, [ptrq + xq*4]
                    mov ptrq, dstam
                    movu [ptrq + xq*4], m0
                %%skip_alphaf:
            %else
                ; constants for STORE16
                movu m5,  [pd_65535f]
                %if mmsize > 16
                    movu xm6, [pb_lo_pack_shuffle16]
                    movu xm7, [pb_hi_pack_shuffle16]
                %endif

                xorps m15, m15, m15
                STORE16 dstrm, 0
                STORE16 dstgm, 1
                STORE16 dstbm, 2

                cmp has_alphad, 0
                je %%skip_alpha
                    %if mmsize > 16
                        mov ptrq, srcam
                        movu xm0, [ptrq + xq*2]
                        mov ptrq, dstam
                        movu [ptrq + xq*2], xm0
                    %else
                        mov ptrq, srcam
                        movsd xm0, [ptrq + xq*2]
                        mov ptrq, dstam
                        movsd [ptrq + xq*2], xm0
                    %endif

                %%skip_alpha:
            %endif

            add xq, mmsize/4
            cmp xd, widthd
            jl %%loop_x

        INC_DATA_PTR srcrm, src_imageq, 2
        INC_DATA_PTR srcgm, src_imageq, 0
        INC_DATA_PTR srcbm, src_imageq, 1
        INC_DATA_PTR srcam, src_imageq, 3

        INC_DATA_PTR dstrm, dst_imageq, 2
        INC_DATA_PTR dstgm, dst_imageq, 0
        INC_DATA_PTR dstbm, dst_imageq, 1
        INC_DATA_PTR dstam, dst_imageq, 3

        inc slice_startd
        cmp slice_startd, slice_endd
        jl %%loop_y

    RET
%endmacro
%if ARCH_X86_64
    %if HAVE_AVX2_EXTERNAL
        INIT_YMM avx2
        DEFINE_INTERP_FUNC tetrahedral, pf32, 32, 1
        DEFINE_INTERP_FUNC tetrahedral, p16, 16, 0
    %endif
    %if HAVE_AVX_EXTERNAL
        INIT_YMM avx
        DEFINE_INTERP_FUNC tetrahedral, pf32, 32, 1
        DEFINE_INTERP_FUNC tetrahedral, p16, 16, 0
    %endif
    INIT_XMM sse2
    DEFINE_INTERP_FUNC tetrahedral, pf32, 32, 1
    DEFINE_INTERP_FUNC tetrahedral, p16, 16, 0
%endif
