;******************************************************************************
;* x86-optimized vertical line scaling functions
;* Copyright (c) 2011 Ronald S. Bultje <rsbultje@gmail.com>
;*                    Kieran Kunhya <kieran@kunhya.com>
;*           (c) 2020 Nelson Gomez <nelson.gomez@microsoft.com>
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

SECTION_RODATA 32

minshort:      times 8 dw 0x8000
yuv2yuvX_16_start:  times 4 dd 0x4000 - 0x40000000
yuv2yuvX_10_start:  times 4 dd 0x10000
yuv2yuvX_9_start:   times 4 dd 0x20000
yuv2yuvX_10_upper:  times 8 dw 0x3ff
yuv2yuvX_9_upper:   times 8 dw 0x1ff
pd_4:          times 4 dd 4
pd_4min0x40000:times 4 dd 4 - (0x40000)
pw_16:         times 8 dw 16
pw_32:         times 8 dw 32
pd_255:        times 8 dd 255
pw_512:        times 8 dw 512
pw_1024:       times 8 dw 1024
pd_65535_invf:             times 8 dd 0x37800080 ;1.0/65535.0
pd_yuv2gbrp16_start:       times 8 dd -0x40000000
pd_yuv2gbrp_y_start:       times 8 dd  (1 << 9)
pd_yuv2gbrp_uv_start:      times 8 dd  ((1 << 9) - (128 << 19))
pd_yuv2gbrp_a_start:       times 8 dd  (1 << 18)
pd_yuv2gbrp16_offset:      times 8 dd  0x10000  ;(1 << 16)
pd_yuv2gbrp16_round13:     times 8 dd  0x02000  ;(1 << 13)
pd_yuv2gbrp16_a_offset:    times 8 dd  0x20002000
pd_yuv2gbrp16_upper30:     times 8 dd  0x3FFFFFFF ;(1<<30) - 1
pd_yuv2gbrp16_upper27:     times 8 dd  0x07FFFFFF ;(1<<27) - 1
pd_yuv2gbrp16_upperC:      times 8 dd  0xC0000000
pb_pack_shuffle8:       db  0,  4,  8, 12, \
                           -1, -1, -1, -1, \
                           -1, -1, -1, -1, \
                           -1, -1, -1, -1, \
                           -1, -1, -1, -1, \
                            0,  4,  8, 12, \
                           -1, -1, -1, -1, \
                           -1, -1, -1, -1
pb_pack_shuffle16le:    db  0,  1,  4,  5, \
                            8,  9, 12, 13, \
                           -1, -1, -1, -1, \
                           -1, -1, -1, -1, \
                           -1, -1, -1, -1, \
                           -1, -1, -1, -1, \
                            0,  1,  4,  5, \
                            8,  9, 12, 13
pb_pack_shuffle16be:    db  1,  0,  5,  4, \
                            9,  8, 13, 12, \
                           -1, -1, -1, -1, \
                           -1, -1, -1, -1, \
                           -1, -1, -1, -1, \
                           -1, -1, -1, -1, \
                            1,  0,  5,  4, \
                            9,  8, 13, 12
pb_shuffle32be:         db  3,  2,  1,  0, \
                            7,  6,  5,  4, \
                           11, 10,  9,  8, \
                           15, 14, 13, 12, \
                            3,  2,  1,  0, \
                            7,  6,  5,  4, \
                           11, 10,  9,  8, \
                           15, 14, 13, 12
yuv2nv12_shuffle_mask: times 2 db 0,  4,  8, 12, \
                                 -1, -1, -1, -1, \
                                 -1, -1, -1, -1, \
                                 -1, -1, -1, -1
yuv2nv21_shuffle_mask: times 2 db 4,  0, 12,  8, \
                                 -1, -1, -1, -1, \
                                 -1, -1, -1, -1, \
                                 -1, -1, -1, -1
yuv2nv12_permute_mask: dd 0, 4, 1, 2, 3, 5, 6, 7

SECTION .text

;-----------------------------------------------------------------------------
; vertical line scaling
;
; void yuv2plane1_<output_size>_<opt>(const int16_t *src, uint8_t *dst, int dstW,
;                                     const uint8_t *dither, int offset)
; and
; void yuv2planeX_<output_size>_<opt>(const int16_t *filter, int filterSize,
;                                     const int16_t **src, uint8_t *dst, int dstW,
;                                     const uint8_t *dither, int offset)
;
; Scale one or $filterSize lines of source data to generate one line of output
; data. The input is 15 bits in int16_t if $output_size is [8,10] and 19 bits in
; int32_t if $output_size is 16. $filter is 12 bits. $filterSize is a multiple
; of 2. $offset is either 0 or 3. $dither holds 8 values.
;-----------------------------------------------------------------------------
%macro yuv2planeX_mainloop 2
.pixelloop_%2:
%assign %%i 0
    ; the rep here is for the 8-bit output MMX case, where dither covers
    ; 8 pixels but we can only handle 2 pixels per register, and thus 4
    ; pixels per iteration. In order to not have to keep track of where
    ; we are w.r.t. dithering, we unroll the MMX/8-bit loop x2.
%if %1 == 8
%assign %%repcnt 16/mmsize
%else
%assign %%repcnt 1
%endif

%rep %%repcnt

%if %1 == 8
%if ARCH_X86_32
    mova            m2, [rsp+mmsize*(0+%%i)]
    mova            m1, [rsp+mmsize*(1+%%i)]
%else ; x86-64
    mova            m2,  m8
    mova            m1,  m_dith
%endif ; x86-32/64
%else ; %1 == 9/10/16
    mova            m1, [yuv2yuvX_%1_start]
    mova            m2,  m1
%endif ; %1 == 8/9/10/16
    movsx     cntr_reg,  fltsizem
.filterloop_%2_ %+ %%i:
    ; input pixels
    mov             r6, [srcq+gprsize*cntr_reg-2*gprsize]
%if %1 == 16
    mova            m3, [r6+r5*4]
    mova            m5, [r6+r5*4+mmsize]
%else ; %1 == 8/9/10
    mova            m3, [r6+r5*2]
%endif ; %1 == 8/9/10/16
    mov             r6, [srcq+gprsize*cntr_reg-gprsize]
%if %1 == 16
    mova            m4, [r6+r5*4]
    mova            m6, [r6+r5*4+mmsize]
%else ; %1 == 8/9/10
    mova            m4, [r6+r5*2]
%endif ; %1 == 8/9/10/16

    ; coefficients
    movd            m0, [filterq+2*cntr_reg-4] ; coeff[0], coeff[1]
%if %1 == 16
    pshuflw         m7,  m0,  0          ; coeff[0]
    pshuflw         m0,  m0,  0x55       ; coeff[1]
    pmovsxwd        m7,  m7              ; word -> dword
    pmovsxwd        m0,  m0              ; word -> dword

    pmulld          m3,  m7
    pmulld          m5,  m7
    pmulld          m4,  m0
    pmulld          m6,  m0

    paddd           m2,  m3
    paddd           m1,  m5
    paddd           m2,  m4
    paddd           m1,  m6
%else ; %1 == 10/9/8
    punpcklwd       m5,  m3,  m4
    punpckhwd       m3,  m4
    SPLATD          m0

    pmaddwd         m5,  m0
    pmaddwd         m3,  m0

    paddd           m2,  m5
    paddd           m1,  m3
%endif ; %1 == 8/9/10/16

    sub       cntr_reg,  2
    jg .filterloop_%2_ %+ %%i

%if %1 == 16
    psrad           m2,  31 - %1
    psrad           m1,  31 - %1
%else ; %1 == 10/9/8
    psrad           m2,  27 - %1
    psrad           m1,  27 - %1
%endif ; %1 == 8/9/10/16

%if %1 == 8
    packssdw        m2,  m1
    packuswb        m2,  m2
    movh   [dstq+r5*1],  m2
%else ; %1 == 9/10/16
%if %1 == 16
    packssdw        m2,  m1
    paddw           m2, [minshort]
%else ; %1 == 9/10
%if cpuflag(sse4)
    packusdw        m2,  m1
%else ; mmxext/sse2
    packssdw        m2,  m1
    pmaxsw          m2,  m6
%endif ; mmxext/sse2/sse4/avx
    pminsw          m2, [yuv2yuvX_%1_upper]
%endif ; %1 == 9/10/16
    mov%2   [dstq+r5*2],  m2
%endif ; %1 == 8/9/10/16

    add             r5,  mmsize/2
    sub             wd,  mmsize/2

%assign %%i %%i+2
%endrep
    jg .pixelloop_%2
%endmacro

%macro yuv2planeX_fn 3

%if ARCH_X86_32
%define cntr_reg fltsizeq
%define movsx mov
%else
%define cntr_reg r7
%define movsx movsxd
%endif

cglobal yuv2planeX_%1, %3, 8, %2, filter, fltsize, src, dst, w, dither, offset
%if %1 == 8 || %1 == 9 || %1 == 10
    pxor            m6,  m6
%endif ; %1 == 8/9/10

%if %1 == 8
%if ARCH_X86_32
%assign pad 0x2c - (stack_offset & 15)
    SUB             rsp, pad
%define m_dith m7
%else ; x86-64
%define m_dith m9
%endif ; x86-32

    ; create registers holding dither
    movq        m_dith, [ditherq]        ; dither
    test        offsetd, offsetd
    jz              .no_rot
%if mmsize == 16
    punpcklqdq  m_dith,  m_dith
%endif ; mmsize == 16
    PALIGNR     m_dith,  m_dith,  3,  m0
.no_rot:
%if mmsize == 16
    punpcklbw   m_dith,  m6
%if ARCH_X86_64
    punpcklwd       m8,  m_dith,  m6
    pslld           m8,  12
%else ; x86-32
    punpcklwd       m5,  m_dith,  m6
    pslld           m5,  12
%endif ; x86-32/64
    punpckhwd   m_dith,  m6
    pslld       m_dith,  12
%if ARCH_X86_32
    mova      [rsp+ 0],  m5
    mova      [rsp+16],  m_dith
%endif
%else ; mmsize == 8
    punpcklbw       m5,  m_dith,  m6
    punpckhbw   m_dith,  m6
    punpcklwd       m4,  m5,  m6
    punpckhwd       m5,  m6
    punpcklwd       m3,  m_dith,  m6
    punpckhwd   m_dith,  m6
    pslld           m4,  12
    pslld           m5,  12
    pslld           m3,  12
    pslld       m_dith,  12
    mova      [rsp+ 0],  m4
    mova      [rsp+ 8],  m5
    mova      [rsp+16],  m3
    mova      [rsp+24],  m_dith
%endif ; mmsize == 8/16
%endif ; %1 == 8

    xor             r5,  r5

%if mmsize == 8 || %1 == 8
    yuv2planeX_mainloop %1, a
%else ; mmsize == 16
    test          dstq, 15
    jnz .unaligned
    yuv2planeX_mainloop %1, a
    REP_RET
.unaligned:
    yuv2planeX_mainloop %1, u
%endif ; mmsize == 8/16

%if %1 == 8
%if ARCH_X86_32
    ADD             rsp, pad
    RET
%else ; x86-64
    REP_RET
%endif ; x86-32/64
%else ; %1 == 9/10/16
    REP_RET
%endif ; %1 == 8/9/10/16
%endmacro

%if ARCH_X86_32
INIT_MMX mmxext
yuv2planeX_fn  8,  0, 7
yuv2planeX_fn  9,  0, 5
yuv2planeX_fn 10,  0, 5
%endif

INIT_XMM sse2
yuv2planeX_fn  8, 10, 7
yuv2planeX_fn  9,  7, 5
yuv2planeX_fn 10,  7, 5

INIT_XMM sse4
yuv2planeX_fn  8, 10, 7
yuv2planeX_fn  9,  7, 5
yuv2planeX_fn 10,  7, 5
yuv2planeX_fn 16,  8, 5

%if HAVE_AVX_EXTERNAL
INIT_XMM avx
yuv2planeX_fn  8, 10, 7
yuv2planeX_fn  9,  7, 5
yuv2planeX_fn 10,  7, 5
%endif

; %1=outout-bpc, %2=alignment (u/a)
%macro yuv2plane1_mainloop 2
.loop_%2:
%if %1 == 8
    paddsw          m0, m2, [srcq+wq*2+mmsize*0]
    paddsw          m1, m3, [srcq+wq*2+mmsize*1]
    psraw           m0, 7
    psraw           m1, 7
    packuswb        m0, m1
    mov%2    [dstq+wq], m0
%elif %1 == 16
    paddd           m0, m4, [srcq+wq*4+mmsize*0]
    paddd           m1, m4, [srcq+wq*4+mmsize*1]
    paddd           m2, m4, [srcq+wq*4+mmsize*2]
    paddd           m3, m4, [srcq+wq*4+mmsize*3]
    psrad           m0, 3
    psrad           m1, 3
    psrad           m2, 3
    psrad           m3, 3
%if cpuflag(sse4) ; avx/sse4
    packusdw        m0, m1
    packusdw        m2, m3
%else ; mmx/sse2
    packssdw        m0, m1
    packssdw        m2, m3
    paddw           m0, m5
    paddw           m2, m5
%endif ; mmx/sse2/sse4/avx
    mov%2    [dstq+wq*2+mmsize*0], m0
    mov%2    [dstq+wq*2+mmsize*1], m2
%else ; %1 == 9/10
    paddsw          m0, m2, [srcq+wq*2+mmsize*0]
    paddsw          m1, m2, [srcq+wq*2+mmsize*1]
    psraw           m0, 15 - %1
    psraw           m1, 15 - %1
    pmaxsw          m0, m4
    pmaxsw          m1, m4
    pminsw          m0, m3
    pminsw          m1, m3
    mov%2    [dstq+wq*2+mmsize*0], m0
    mov%2    [dstq+wq*2+mmsize*1], m1
%endif
    add             wq, mmsize
    jl .loop_%2
%endmacro

%macro yuv2plane1_fn 3
cglobal yuv2plane1_%1, %3, %3, %2, src, dst, w, dither, offset
    movsxdifnidn    wq, wd
    add             wq, mmsize - 1
    and             wq, ~(mmsize - 1)
%if %1 == 8
    add           dstq, wq
%else ; %1 != 8
    lea           dstq, [dstq+wq*2]
%endif ; %1 == 8
%if %1 == 16
    lea           srcq, [srcq+wq*4]
%else ; %1 != 16
    lea           srcq, [srcq+wq*2]
%endif ; %1 == 16
    neg             wq

%if %1 == 8
    pxor            m4, m4               ; zero

    ; create registers holding dither
    movq            m3, [ditherq]        ; dither
    test       offsetd, offsetd
    jz              .no_rot
%if mmsize == 16
    punpcklqdq      m3, m3
%endif ; mmsize == 16
    PALIGNR         m3, m3, 3, m2
.no_rot:
%if mmsize == 8
    mova            m2, m3
    punpckhbw       m3, m4               ; byte->word
    punpcklbw       m2, m4               ; byte->word
%else
    punpcklbw       m3, m4
    mova            m2, m3
%endif
%elif %1 == 9
    pxor            m4, m4
    mova            m3, [pw_512]
    mova            m2, [pw_32]
%elif %1 == 10
    pxor            m4, m4
    mova            m3, [pw_1024]
    mova            m2, [pw_16]
%else ; %1 == 16
%if cpuflag(sse4) ; sse4/avx
    mova            m4, [pd_4]
%else ; mmx/sse2
    mova            m4, [pd_4min0x40000]
    mova            m5, [minshort]
%endif ; mmx/sse2/sse4/avx
%endif ; %1 == ..

    ; actual pixel scaling
%if mmsize == 8
    yuv2plane1_mainloop %1, a
%else ; mmsize == 16
    test          dstq, 15
    jnz .unaligned
    yuv2plane1_mainloop %1, a
    REP_RET
.unaligned:
    yuv2plane1_mainloop %1, u
%endif ; mmsize == 8/16
    REP_RET
%endmacro

%if ARCH_X86_32
INIT_MMX mmx
yuv2plane1_fn  8, 0, 5
yuv2plane1_fn 16, 0, 3

INIT_MMX mmxext
yuv2plane1_fn  9, 0, 3
yuv2plane1_fn 10, 0, 3
%endif

INIT_XMM sse2
yuv2plane1_fn  8, 5, 5
yuv2plane1_fn  9, 5, 3
yuv2plane1_fn 10, 5, 3
yuv2plane1_fn 16, 6, 3

INIT_XMM sse4
yuv2plane1_fn 16, 5, 3

%if HAVE_AVX_EXTERNAL
INIT_XMM avx
yuv2plane1_fn  8, 5, 5
yuv2plane1_fn  9, 5, 3
yuv2plane1_fn 10, 5, 3
yuv2plane1_fn 16, 5, 3
%endif

%undef movsx

;-----------------------------------------------------------------------------
; AVX2 yuv2nv12cX implementation
;
; void ff_yuv2nv12cX_avx2(enum AVPixelFormat format, const uint8_t *dither,
;                         const int16_t *filter, int filterSize,
;                         const int16_t **u, const int16_t **v,
;                         uint8_t *dst, int dstWidth)
;
; void ff_yuv2nv21cX_avx2(enum AVPixelFormat format, const uint8_t *dither,
;                         const int16_t *filter, int filterSize,
;                         const int16_t **u, const int16_t **v,
;                         uint8_t *dst, int dstWidth)
;-----------------------------------------------------------------------------

%if ARCH_X86_64
%macro yuv2nv12cX_fn 1
cglobal %1cX, 8, 11, 13, tmp1, dither, filter, filterSize, u, v, dst, dstWidth

    mov tmp1q, qword [ditherq]
    movq xm0, tmp1q
    ror tmp1q, 24
    movq xm1, tmp1q

    pmovzxbd m0, xm0
    pslld m0, m0, 12                        ; ditherLo
    pmovzxbd m1, xm1
    pslld m1, m1, 12                        ; ditherHi

    pxor m9, m9                             ; uint8_min dwords
    mova m10, [pd_255]                      ; uint8_max dwords
    mova m11, [%1_shuffle_mask]             ; shuffle_mask
    mova m12, [yuv2nv12_permute_mask]       ; permute mask

    DEFINE_ARGS tmp1, tmp2, filter, filterSize, u, v, dst, dstWidth

    xor r8q, r8q

nv12_outer_%1:
    mova m2, m0                             ; resultLo
    mova m3, m1                             ; resultHi
    xor r9q, r9q

nv12_inner_%1:
    movsx r10d, word [filterq + (2 * r9q)]
    movd xm4, r10d
    vpbroadcastd m4, xm4                    ; filter

    mov tmp1q, [uq + (gprsize * r9q)]
    mova xm7, oword [tmp1q + 2 * r8q]

    mov tmp2q, [vq + (gprsize * r9q)]
    mova xm8, oword [tmp2q + 2 * r8q]

    punpcklwd xm5, xm7, xm8
    pmovsxwd m5, xm5                        ; multiplicandsLo
    punpckhwd xm6, xm7, xm8
    pmovsxwd m6, xm6                        ; multiplicandsHi

    pmulld m7, m5, m4                       ; mulResultLo
    pmulld m8, m6, m4                       ; mulResultHi
    paddd m2, m2, m7                        ; resultLo += mulResultLo
    paddd m3, m3, m8                        ; resultHi += mulResultHi

    inc r9d
    cmp r9d, filterSized
    jl nv12_inner_%1
    ; end of inner loop

    psrad m2, m2, 19
    psrad m3, m3, 19

    ; Vectorized av_clip_uint8
    pmaxsd m2, m2, m9
    pmaxsd m3, m3, m9
    pminsd m2, m2, m10
    pminsd m3, m3, m10

    ; At this point we have clamped uint8s arranged in this order:
    ;     m2: u1  0  0  0  v1  0  0  0  [...]
    ;     m3: u5  0  0  0  v5  0  0  0  [...]
    ;
    ; First, we shuffle the bytes to make the bytes semi-contiguous.
    ; AVX-2 doesn't have cross-lane shuffling, so we'll end up with:
    ;     m2: u1  v1  u2  v2  0  0  0  0  0  0  0  0  u3  v3  u4  v4
    ;     m3: u5  v5  u6  v6  0  0  0  0  0  0  0  0  u7  v7  u8  v8
    pshufb m2, m2, m11
    pshufb m3, m3, m11

    ; To fix the cross-lane shuffling issue, we'll then use cross-lane
    ; permutation to combine the two segments
    vpermd m2, m12, m2
    vpermd m3, m12, m3

    ; Now we have the final results in the lower 8 bytes of each register
    movq [dstq], xm2
    movq [dstq + 8], xm3

    add r8d, 8
    add dstq, 16

    cmp r8d, dstWidthd
    jl nv12_outer_%1
    RET
%endmacro

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
yuv2nv12cX_fn yuv2nv12
yuv2nv12cX_fn yuv2nv21
%endif
%endif ; ARCH_X86_64

;-----------------------------------------------------------------------------
; planar grb yuv2anyX functions
; void ff_yuv2<gbr_format>_full_X_<opt>(SwsContext *c, const int16_t *lumFilter,
;                                       const int16_t **lumSrcx, int lumFilterSize,
;                                       const int16_t *chrFilter, const int16_t **chrUSrcx,
;                                       const int16_t **chrVSrcx, int chrFilterSize,
;                                       const int16_t **alpSrcx, uint8_t **dest,
;                                       int dstW, int y)
;-----------------------------------------------------------------------------

%if ARCH_X86_64
struc SwsContext
    .padding:           resb 40292 ; offsetof(SwsContext, yuv2rgb_y_offset)
    .yuv2rgb_y_offset:  resd 1
    .yuv2rgb_y_coeff:   resd 1
    .yuv2rgb_v2r_coeff: resd 1
    .yuv2rgb_v2g_coeff: resd 1
    .yuv2rgb_u2g_coeff: resd 1
    .yuv2rgb_u2b_coeff: resd 1
endstruc

%define R m0
%define G m1
%define B m2
%define A m3

%define Y m4
%define U m5
%define V m6

; Clip a signed integer to an unsigned power of two range.
; av_clip_uintp2
; 1 - dest
; 2 - bit position to clip at
%macro CLIPP2 2
    ; (~a) >> 31 & ((1<<p) - 1);
    pcmpeqb m4, m4
    pxor m4, %1
    psrad m4, 31
    movu m5, [pd_yuv2gbrp16_upper%2]
    pand m4, m5

    ; (a & ~((1<<p) - 1)) == 0
    pandn m5, %1
    pxor m6, m6
    pcmpeqd m5, m6
%if cpuflag(avx2)
    vpblendvb %1, m4, %1, m5
%else
    pxor %1, m4
    pand %1, m5
    pxor %1, m4
%endif
%endmacro

; 1 - dest
; 2 - source
%macro LOAD16 2
    %if cpuflag(avx2)
        movu xm%1, %2
        vpmovsxwd m%1, xm%1
    %elif cpuflag(sse4)
        movsd m%1, %2
        pmovsxwd m%1, m%1
    %else
        movsd m%1, %2
        punpcklwd m%1, m%1
        psrad m%1, 16 ; sign extend
    %endif
%endmacro

; 1 - dest
; 2 - source
; 3 - depth
%macro LOAD_PIXELS 3
    mov ptrq, [%2 + jq*8]
%if %3 >= 16
    movu m%1, [ptrq + xq*4]
%else
    LOAD16 %1, [ptrq + xq*2]
%endif
%endmacro

; 1 - dest
; 2 - source
%macro STORE8 2
    mov ptrq, %1
    %if mmsize > 16
        pshufb m%2, [pb_pack_shuffle8]
        vextractf128 xm4, m%2, 1
        por xm%2, xm4
        movq [ptrq + xq], xm%2
    %else
        %if cpuflag(sse4)
            pshufb m%2, [pb_pack_shuffle8]
        %else
            psrldq m4, m%2, 3
            por m%2, m4
            psrldq m4, m%2, 6
            por m%2, m4
        %endif
        movd [ptrq + xq], m%2
    %endif
%endmacro

; 1 - dest
; 2 - source
; 3 - is big endian
%macro STORE16 3
    mov ptrq, %1
    %if mmsize > 16
        %if %3 ; bigendian
            pshufb m%2, [pb_pack_shuffle16be]
        %else
            pshufb m%2, [pb_pack_shuffle16le]
        %endif
        vpermq m%2, m%2, (3 << 6 | 0 << 4 | 3 << 2 | 0 << 0)
        movu [ptrq + xq*2], xm%2
    %else
        %if cpuflag(sse4) && %3 ; bigendian
            pshufb m%2, [pb_pack_shuffle16be]
        %elif cpuflag(sse4)
            pshufb m%2, [pb_pack_shuffle16le]
        %else
            pshuflw m%2, m%2, (1 << 6 | 1 << 4 | 2 << 2 | 0 << 0)
            pshufhw m%2, m%2, (1 << 6 | 1 << 4 | 2 << 2 | 0 << 0)
            pshufd  m%2, m%2, (3 << 6 | 3 << 4 | 2 << 2 | 0 << 0)
            %if %3 ; bigendian
                psrlw  m4, m%2, 8
                psllw  m%2, 8
                por m%2, m4
            %endif
        %endif
        movq [ptrq + xq*2], m%2
    %endif
%endmacro

%macro SWAP32 1
%if mmsize > 16 || cpuflag(sse4)
    pshufb m%1, [pb_shuffle32be]
%else
    psrlw  m4, m%1, 8
    psllw  m%1, 8
    por m%1, m4
    pshuflw m%1, m%1, (2 << 6 | 3 << 4 | 0 << 2 | 1 << 0)
    pshufhw m%1, m%1, (2 << 6 | 3 << 4 | 0 << 2 | 1 << 0)
%endif
%endmacro

; 1 - dest
; 2 - source
; 3 - depth
; 4 - is big endian
%macro STORE_PIXELS 4
%if %3 > 16
    %if %4
        SWAP32 %2
    %endif
    mov ptrq, %1
    movu [ptrq + xq*4], m%2
%elif %3 > 8
    STORE16 %1, %2, %4
%else
    STORE8 %1, %2
%endif
%endmacro

%macro PMULLO 3
%if cpuflag(sse4) || mmsize > 16
    pmulld %1, %2, %3
%else
    %ifidni %1, %2
    %else
        mova %1, %2
    %endif
    pshufd m7, %1, (2 << 6 | 3 << 4 | 0 << 2 | 1 << 0) ; 0xb1
    pshufd m8, %3, (2 << 6 | 3 << 4 | 0 << 2 | 1 << 0) ; 0xb1
    pmuludq m7, m8
    pshufd  m7, m7, (3 << 6 | 1 << 4 | 2 << 2 | 0 << 0) ; 0xd8
    pmuludq %1, %3
    pshufd  %1, %1, (3 << 6 | 1 << 4 | 2 << 2 | 0 << 0) ; 0xd8
    punpckldq %1, m7
%endif
%endmacro

; 1 - name
; 2 - depth
; 3 - has alpha
; 3 - is big endian
; 5 - is float
%macro yuv2gbrp_fn 5
%define DEPTH %2
%define HAS_ALPHA %3
%define IS_BE %4
%define FLOAT %5
%define SH (22 + 8 - DEPTH)

%if DEPTH >= 16
    %define RGB_SHIFT 14
    %define A_SHIFT 14
%elif 22 != SH
    %define RGB_SHIFT SH
    %define A_SHIFT (SH-3)
%else
    %define RGB_SHIFT 22
    %define A_SHIFT 19
%endif

%if DEPTH >= 16
    %define YUV_SHIFT 14
    %define Y_START  m9
    %define Y_ROUND [pd_yuv2gbrp16_round13]
    %define UV_START m9
    %define A_START  m9
    %define A_CLIP2P 30
%else
    %define YUV_SHIFT 10
    %define Y_START  [pd_yuv2gbrp_y_start]
    %define Y_ROUND  m9
    %define UV_START [pd_yuv2gbrp_uv_start]
    %define A_START  [pd_yuv2gbrp_a_start]
    %define A_CLIP2P 27
%endif

cglobal yuv2%1_full_X, 12, 14, 16, ptr, lumFilter, lumSrcx, lumFilterSize, chrFilter, chrUSrcx, chrVSrcx, chrFilterSize, alpSrcx, dest, dstW, y, x, j
    VBROADCASTSS m10, dword [ptrq + SwsContext.yuv2rgb_y_offset]
    VBROADCASTSS m11, dword [ptrq + SwsContext.yuv2rgb_y_coeff]
    VBROADCASTSS m12, dword [ptrq + SwsContext.yuv2rgb_v2r_coeff]
    VBROADCASTSS m13, dword [ptrq + SwsContext.yuv2rgb_v2g_coeff]
    VBROADCASTSS m14, dword [ptrq + SwsContext.yuv2rgb_u2g_coeff]
    VBROADCASTSS m15, dword [ptrq + SwsContext.yuv2rgb_u2b_coeff]

%if DEPTH >= 16
    movu m9, [pd_yuv2gbrp16_start]
%else
    mov xq, (1 << (SH-1))
    movq xm9, xq
    VBROADCASTSS m9, xm9
%endif
    xor xq, xq

    %%loop_x:
        movu Y, Y_START
        movu U, UV_START
        movu V, UV_START

        xor jq, jq
        %%loop_luma:
            movsx ptrd, word [lumFilterq + jq*2]
            movd xm0, ptrd
            VBROADCASTSS m0, xm0
            LOAD_PIXELS 1, lumSrcxq, DEPTH
            PMULLO m1, m1, m0
            paddd Y, m1
            inc jd
            cmp jd, lumFilterSized
            jl %%loop_luma

%if HAS_ALPHA
        cmp alpSrcxq, 0
        je %%skip_alpha_load
            xor jq, jq
            movu A, A_START
            %%loop_alpha:
                movsx ptrd, word [lumFilterq + jq*2]
                movd xm0, ptrd
                VBROADCASTSS m0, xm0
                LOAD_PIXELS 1, alpSrcxq, DEPTH
                PMULLO m1, m1, m0
                paddd A, m1
                inc jd
                cmp jd, lumFilterSized
                jl %%loop_alpha
%if DEPTH >= 16
            psrad A, 1
            paddd A, [pd_yuv2gbrp16_a_offset]
%endif
        %%skip_alpha_load:
%endif
        xor jq, jq
        %%loop_chr:
            movsx ptrd, word [chrFilterq + jq*2]
            movd xm0, ptrd
            VBROADCASTSS m0, xm0
            LOAD_PIXELS 1, chrUSrcxq, DEPTH
            LOAD_PIXELS 2, chrVSrcxq, DEPTH
            PMULLO m1, m1, m0
            PMULLO m2, m2, m0
            paddd U, m1
            paddd V, m2
            inc jd
            cmp jd, chrFilterSized
            jl %%loop_chr

        psrad Y, YUV_SHIFT
%if  DEPTH >= 16
        paddd Y, [pd_yuv2gbrp16_offset]
%endif
        psrad U, YUV_SHIFT
        psrad V, YUV_SHIFT

        psubd  Y, m10    ; yuv2rgb_y_offset
        PMULLO Y, Y, m11 ; yuv2rgb_y_coeff
        paddd  Y, Y_ROUND

        PMULLO R, V, m12 ; yuv2rgb_v2r_coeff
        PMULLO B, U, m15 ; yuv2rgb_u2b_coeff

        PMULLO U, U, m14 ; yuv2rgb_u2g_coeff
        PMULLO V, V, m13 ; yuv2rgb_v2g_coeff
        paddd G, U, V
        paddd R, Y
        paddd G, Y
        paddd B, Y

        CLIPP2 R, 30
        CLIPP2 G, 30
        CLIPP2 B, 30

        psrad R, RGB_SHIFT
        psrad G, RGB_SHIFT
        psrad B, RGB_SHIFT

%if FLOAT
        cvtdq2ps R, R
        cvtdq2ps G, G
        cvtdq2ps B, B
        mulps R, [pd_65535_invf]
        mulps G, [pd_65535_invf]
        mulps B, [pd_65535_invf]
%endif
        STORE_PIXELS [destq +  0], 1, DEPTH, IS_BE ; G
        STORE_PIXELS [destq +  8], 2, DEPTH, IS_BE ; B
        STORE_PIXELS [destq + 16], 0, DEPTH, IS_BE ; R

%if HAS_ALPHA
        cmp alpSrcxq, 0
        je %%skip_alpha_store
            CLIPP2 A, A_CLIP2P
            psrad A, A_SHIFT
%if FLOAT
            cvtdq2ps A, A
            mulps A, [pd_65535_invf]
%endif
            STORE_PIXELS [destq + 24], 3, DEPTH, IS_BE
        %%skip_alpha_store:
%endif
        add xq, mmsize/4
        cmp xd, dstWd
        jl %%loop_x

    RET
%endmacro

%macro yuv2gbrp_fn_decl 2
INIT_%1 %2
yuv2gbrp_fn gbrp,        8, 0, 0, 0
yuv2gbrp_fn gbrap,       8, 1, 0, 0
yuv2gbrp_fn gbrp9le,     9, 0, 0, 0
yuv2gbrp_fn gbrp10le,   10, 0, 0, 0
yuv2gbrp_fn gbrap10le,  10, 1, 0, 0
yuv2gbrp_fn gbrp12le,   12, 0, 0, 0
yuv2gbrp_fn gbrap12le,  12, 1, 0, 0
yuv2gbrp_fn gbrp14le,   14, 0, 0, 0
yuv2gbrp_fn gbrp16le,   16, 0, 0, 0
yuv2gbrp_fn gbrap16le,  16, 1, 0, 0
yuv2gbrp_fn gbrpf32le,  32, 0, 0, 1
yuv2gbrp_fn gbrapf32le, 32, 1, 0, 1

yuv2gbrp_fn gbrp9be,     9, 0, 1, 0
yuv2gbrp_fn gbrp10be,   10, 0, 1, 0
yuv2gbrp_fn gbrap10be,  10, 1, 1, 0
yuv2gbrp_fn gbrp12be,   12, 0, 1, 0
yuv2gbrp_fn gbrap12be,  12, 1, 1, 0
yuv2gbrp_fn gbrp14be,   14, 0, 1, 0
yuv2gbrp_fn gbrp16be,   16, 0, 1, 0
yuv2gbrp_fn gbrap16be,  16, 1, 1, 0
yuv2gbrp_fn gbrpf32be,  32, 0, 1, 1
yuv2gbrp_fn gbrapf32be, 32, 1, 1, 1
%endmacro

yuv2gbrp_fn_decl XMM, sse2
yuv2gbrp_fn_decl XMM, sse4

%if HAVE_AVX2_EXTERNAL
yuv2gbrp_fn_decl YMM, avx2
%endif

%endif ; ARCH_X86_64
