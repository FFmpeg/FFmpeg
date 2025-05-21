;******************************************************************************
;* VP9 IDCT SIMD optimizations
;*
;* Copyright (C) 2025 Two Orioles, LLC
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

%if ARCH_X86_64 && HAVE_AVX512ICL_EXTERNAL

SECTION_RODATA 64

; Thw following set of constants are ordered to form the
; qword shuffle mask { 0,  2,  4,  6,  1,  3,  5,  7 }
%define deintq_perm pd_5520
pd_5520:     dd 5520
pd_9760:     dd 9760
pd_10394:    dd 10394
pd_15426:    dd 15426
pd_804:      dd 804
pd_2404:     dd 2404
pd_6270:     dd 6270
pd_9102:     dd 9102
pd_11585:    dd 11585
pd_12665:    dd 12665
pd_7723:     dd 7723
pd_14811:    dd 14811
pd_7005:     dd 7005
pd_14053:    dd 14053
pd_8423:     dd 8423
pd_13623:    dd 13623

pixel_clip:  times 2 dw 0x7c00
pixel_clip6: dd 2031648 ; 32 + (pixel_clip << 6)
pd_532480:   dd 532480  ; 8192 + (32 << 14)
pd_8192:     dd 8192

pd_1606:     dd 1606
pd_3196:     dd 3196
pd_3981:     dd 3981
pd_4756:     dd 4756
pd_11003:    dd 11003
pd_12140:    dd 12140
pd_13160:    dd 13160
pd_14449:    dd 14449
pd_15137:    dd 15137
pd_15679:    dd 15679
pd_15893:    dd 15893
pd_16069:    dd 16069
pd_16207:    dd 16207
pd_16305:    dd 16305
pd_16364:    dd 16364

SECTION .text

%define o_base (deintq_perm+128)
%define o(x) (r5 - o_base + (x))
%define m(x) mangle(private_prefix %+ _ %+ x %+ SUFFIX)

; dst1 = (src1 * coef1 - src2 * coef2 + rnd) >> 12
; dst2 = (src1 * coef2 + src2 * coef1 + rnd) >> 12
; skip round/shift if rnd is not a number
%macro ITX_MULSUB_2D 8-9 0 ; dst/src[1-2], tmp[1-3], rnd, coef[1-2], inv_dst2
%if %8 < 32
    pmulld              m%4, m%1, m%8
    pmulld              m%3, m%2, m%8
%else
    vpbroadcastd        m%3, [o(pd_%8)]
    pmulld              m%4, m%1, m%3
    pmulld              m%3, m%2
%endif
%if %7 < 32
    pmulld              m%1, m%7
    pmulld              m%2, m%7
%else
    vpbroadcastd        m%5, [o(pd_%7)]
    pmulld              m%1, m%5
    pmulld              m%2, m%5
%endif
%if %9
    psubd               m%4, m%6, m%4
    psubd               m%2, m%4, m%2
%else
%ifnum %6
    paddd               m%4, m%6
%endif
    paddd               m%2, m%4
%endif
%ifnum %6
    paddd               m%1, m%6
%endif
    psubd               m%1, m%3
%ifnum %6
    psrad               m%2, 14
    psrad               m%1, 14
%endif
%endmacro

%macro WRAP_YMM 1+
    INIT_YMM cpuname
    %1
    INIT_ZMM cpuname
%endmacro

%macro TRANSPOSE_4D 5 ; in[1-4], tmp
    punpckhdq           m%5, m%3, m%4 ; c2 d2 c3 d3
    punpckldq           m%3, m%4      ; c0 d0 c1 d1
    punpckhdq           m%4, m%1, m%2 ; a2 b2 a3 b3
    punpckldq           m%1, m%2      ; a0 b0 a1 b1
    punpckhqdq          m%2, m%1, m%3 ; a1 b1 c1 d1
    punpcklqdq          m%1, m%3      ; a0 b0 c0 d0
    punpcklqdq          m%3, m%4, m%5 ; a2 b2 c2 d2
    punpckhqdq          m%4, m%5      ; a3 b3 c3 d3
%endmacro

%macro TRANSPOSE_4DQ 5 ; in[1-4], tmp
    vshufi32x4          m%5, m%3, m%4, q3232 ; c2 c3 d2 d3
    vinserti32x8        m%3, ym%4, 1         ; c0 c1 d0 d1
    vshufi32x4          m%4, m%1, m%2, q3232 ; a2 a3 b2 b3
    vinserti32x8        m%1, ym%2, 1         ; a0 a1 b0 b1
    vshufi32x4          m%2, m%1, m%3, q3131 ; a1 b1 c1 d1
    vshufi32x4          m%1, m%3, q2020      ; a0 b0 c0 d0
    vshufi32x4          m%3, m%4, m%5, q2020 ; a2 b2 c2 d2
    vshufi32x4          m%4, m%5, q3131      ; a3 b3 c3 d3
%endmacro

%macro INV_TXFM_FN 3-4 0 ; type1, type2, size, eob_offset
cglobal vp9_i%1_i%2_%3_add_10, 4, 5, 0, dst, stride, c, eob, tx2
    %define %%p1 m(vp9_i%1_%3_internal_10)
    lea                  r5, [o_base]
    ; Jump to the 1st txfm function if we're not taking the fast path, which
    ; in turn performs an indirect jump to the 2nd txfm function.
    lea                tx2q, [m(vp9_i%2_%3_internal_10).pass2]
%ifidn %1_%2, dct_dct
    dec                eobd
    jnz %%p1
%else
%if %4
    add                eobd, %4
%endif
    ; jump to the 1st txfm function unless it's located directly after this
    times ((%%end - %%p1) >> 31) & 1 jmp %%p1
ALIGN function_align
%%end:
%endif
%endmacro

%macro INV_TXFM_16X16_FN 2-3 0 ; type1, type2, eob_offset
    INV_TXFM_FN          %1, %2, 16x16, %3
%ifidn %1_%2, dct_dct
    imul                r6d, [cq], 11585
    vpbroadcastd        ym3, [o(pixel_clip)]
    mov                [cq], r3d
    add                 r6d, 8192
    sar                 r6d, 14
    imul                r6d, 11585
    or                  r3d, 8
    add                 r6d, 532480
    sar                 r6d, 20
    vpbroadcastw        ym2, r6d
    paddsw              ym2, ym3
.dconly_loop:
    paddsw              ym0, ym2, [dstq+strideq*0]
    paddsw              ym1, ym2, [dstq+strideq*1]
    psubusw             ym0, ym3
    psubusw             ym1, ym3
    mova   [dstq+strideq*0], ym0
    mova   [dstq+strideq*1], ym1
    lea                dstq, [dstq+strideq*2]
    dec                 r3d
    jg .dconly_loop
    RET
%endif
%endmacro

%macro IDCT16_PART1 0
%if mmsize == 64
.main_part1_fast:
%endif
    pmulld              m15, m1, [o(pd_16305)] {bcstd} ; t15a
    pmulld               m1, [o(pd_1606)] {bcstd}      ; t8a
    pmulld               m9, m7, [o(pd_10394)] {bcstd} ; t9a
    pmulld               m7, [o(pd_12665)] {bcstd}     ; t14a
    pmulld              m11, m5, [o(pd_14449)] {bcstd} ; t13a
    pmulld               m5, [o(pd_7723)] {bcstd}      ; t10a
    pmulld              m13, m3, [o(pd_4756)] {bcstd}  ; t11a
    pmulld               m3, [o(pd_15679)] {bcstd}     ; t12a
    pmulld              m10, m6, [o(pd_9102)] {bcstd}  ; t5a
    pmulld               m6, [o(pd_13623)] {bcstd}     ; t6a
    pmulld              m14, m2, [o(pd_16069)] {bcstd} ; t7a
    pmulld               m2, [o(pd_3196)] {bcstd}      ; t4a
    pmulld              m12, m4, [o(pd_15137)] {bcstd} ; t3
    pmulld               m4, [o(pd_6270)] {bcstd}      ; t2
    pmulld               m0, m21
    REPX  {psubd x, m20, x}, m9, m13, m10
    paddd                m0, m20
    mova                m18, m0
%if mmsize == 64 ; for the ymm variant we only ever use the fast path
    jmp %%main_part1b
.main_part1:
    ITX_MULSUB_2D         1, 15, 16, 17, 18, _,  1606, 16305 ; t8a,  t15a
    ITX_MULSUB_2D         9,  7, 16, 17, 18, _, 12665, 10394 ; t9a,  t14a
    ITX_MULSUB_2D         5, 11, 16, 17, 18, _,  7723, 14449 ; t10a, t13a
    ITX_MULSUB_2D        13,  3, 16, 17, 18, _, 15679,  4756 ; t11a, t12a
    ITX_MULSUB_2D        10,  6, 16, 17, 18, _, 13623,  9102 ; t5a,  t6a
    ITX_MULSUB_2D         2, 14, 16, 17, 18, _,  3196, 16069 ; t4a,  t7a
    ITX_MULSUB_2D         4, 12, 16, 17, 18, _,  6270, 15137 ; t2,  t3
    pmulld               m0, m21
    pmulld               m8, m21
    REPX     {paddd x, m20}, m0, m9, m13, m10
    psubd               m18, m0, m8   ; t1
    paddd                m0, m8       ; t0
%%main_part1b:
%endif
    vpbroadcastd        m19, [o(pd_15137)]
    vpbroadcastd        m16, [o(pd_6270)]
    REPX     {paddd x, m20}, m15, m7, m1, m11, m3, m5
    REPX     {psrad x, 14 }, m15, m7, m1, m9, m11, m3, m5, m13
    paddd               m17, m15, m7  ; t15
    psubd               m15, m7       ; t14
    psubd                m7, m3, m11  ; t13
    paddd                m3, m11      ; t12
    psubd               m11, m13, m5  ; t10
    paddd                m5, m13      ; t11
    psubd               m13, m1, m9   ; t9
    paddd                m1, m9       ; t8
    ITX_MULSUB_2D        15, 13, 8, 9, _, 20, 16, 19         ; t9a,  t14a
    ITX_MULSUB_2D         7, 11, 8, 9, _, 20, 16, 19, 2      ; t13a, t10a
    paddd               m16, m1, m5   ; t8a
    psubd                m1, m5       ; t11a
    paddd                m8, m15, m11 ; t9
    psubd               m15, m11      ; t10
    psubd               m11, m17, m3  ; t12a
    paddd               m17, m3       ; t15a
    psubd                m9, m13, m7  ; t13
    paddd               m13, m7       ; t14
    REPX    {pmulld x, m21}, m11, m9, m1, m15
    REPX     {paddd x, m20}, m2, m6, m14
    REPX     {psrad x, 14 }, m10, m2, m6, m14
    psubd                m3, m2, m10  ; t5a
    paddd               m10, m2       ; t4
    paddd               m11, m20
    psubd                m5, m11, m1  ; t11
    paddd               m11, m1       ; t12
    psubd                m1, m14, m6  ; t6a
    paddd               m14, m6       ; t7
    pmulld               m1, m21
    pmulld               m3, m21
    paddd                m4, m20
    paddd               m12, m20
    REPX     {psrad x, 14 }, m4, m12, m0, m18
    paddd                m9, m20
    paddd                m2, m9, m15  ; t13a
    psubd                m9, m15      ; t10a
    paddd                m1, m20
    psubd                m6, m1, m3   ; t5
    paddd                m1, m3       ; t6
    REPX      {psrad x, 14}, m6, m1, m11, m5, m2, m9
%endmacro

%macro IDCT16_PART2 0
    psubd                m3, m0, m12 ; t3
    paddd                m0, m12     ; t0
    psubd               m12, m18, m4 ; t2
    paddd               m18, m4      ; t1
    psubd                m4, m3, m10 ; t4
    paddd                m3, m10     ; t3
    psubd               m10, m12, m6 ; t5
    paddd               m12, m6      ; t2
    psubd                m6, m18, m1 ; t6
    paddd                m1, m18     ; t1
    psubd                m7, m0, m14 ; t7
    paddd                m0, m14     ; t0
    psubd               m15, m0, m17 ; out15
    paddd                m0, m17     ; out0
    psubd               m14, m1, m13 ; out14
    paddd                m1, m13     ; out1
    psubd               m13, m12, m2 ; out13
    paddd                m2, m12     ; out2
    psubd               m12, m3, m11 ; out12
    paddd                m3, m11     ; out3
    psubd               m11, m4, m5  ; out11
    paddd                m4, m5      ; out4
    paddd                m5, m10, m9 ; out5
    psubd               m10, m9      ; out10
    psubd                m9, m6, m8  ; out9
    paddd                m6, m8      ; out6
    psubd                m8, m7, m16 ; out8
    paddd                m7, m16     ; out7
%endmacro

INIT_ZMM avx512icl
INV_TXFM_16X16_FN dct, dct
INV_TXFM_16X16_FN dct, adst, 39-23-1

cglobal vp9_idct_16x16_internal_10, 0, 7, 22, dst, stride, c, eob, tx2
    mova                 m0, [cq+64* 0]
    mova                 m1, [cq+64* 1]
    mova                 m2, [cq+64* 2]
    mova                 m3, [cq+64* 3]
    mova                 m4, [cq+64* 4]
    mova                 m5, [cq+64* 5]
    mova                 m6, [cq+64* 6]
    mova                 m7, [cq+64* 7]
    vpbroadcastd        m20, [o(pd_8192)]
    vpbroadcastd        m21, [o(pd_11585)]
    sub                eobd, 38
    jl .pass1_fast
    mova                 m8, [cq+64* 8]
    mova                 m9, [cq+64* 9]
    mova                m10, [cq+64*10]
    mova                m11, [cq+64*11]
    mova                m12, [cq+64*12]
    mova                m13, [cq+64*13]
    mova                m14, [cq+64*14]
    mova                m15, [cq+64*15]
    call .main_part1
    call .main_part2
.pass1_end:
    TRANSPOSE_4DQ         0,  4,  8, 12, 16
    TRANSPOSE_4DQ         1,  5,  9, 13, 16
    TRANSPOSE_4DQ         2,  6, 10, 14, 16
    TRANSPOSE_4DQ         3,  7, 11, 15, 16
    TRANSPOSE_4D          8,  9, 10, 11, 16
    TRANSPOSE_4D         12, 13, 14, 15, 16
    mov                 r6d, 64*12
    jmp .pass1_transpose_end
.pass1_fast:
    WRAP_YMM IDCT16_PART1
    WRAP_YMM IDCT16_PART2
.pass1_fast_end:
    vinserti32x8         m0, ym4, 1
    vinserti32x8         m8, ym12, 1
    vinserti32x8         m1, ym5, 1
    vinserti32x8         m9, ym13, 1
    vinserti32x8         m2, ym6, 1
    vinserti32x8        m10, ym14, 1
    vinserti32x8         m3, ym7, 1
    vinserti32x8        m11, ym15, 1
    vshufi32x4           m4, m0, m8, q3131
    vshufi32x4           m0, m8, q2020
    vshufi32x4           m5, m1, m9, q3131
    vshufi32x4           m1, m9, q2020
    vshufi32x4           m6, m2, m10, q3131
    vshufi32x4           m2, m10, q2020
    vshufi32x4           m7, m3, m11, q3131
    vshufi32x4           m3, m11, q2020
    mov                 r6d, 64*4
.pass1_transpose_end:
    pxor                m16, m16
.zero_loop:
    mova       [cq+r6+64*0], m16
    mova       [cq+r6+64*1], m16
    mova       [cq+r6+64*2], m16
    mova       [cq+r6+64*3], m16
    sub                 r6d, 64*4
    jge .zero_loop
    TRANSPOSE_4D          0,  1,  2,  3, 16
    TRANSPOSE_4D          4,  5,  6,  7, 16
    jmp                tx2q
.pass2:
    test               eobd, eobd
    jl .pass2_fast
    call .main_part1
    jmp .pass2_end
.pass2_fast:
    call .main_part1_fast
.pass2_end:
    vpbroadcastd         m3, [o(pixel_clip6)]
    paddd                m0, m3
    paddd               m18, m3
    call .main_part2
    REPX       {psrad x, 6}, m0, m1, m2, m3
    packssdw             m0, m1
    lea                  r6, [strideq*3]
    packssdw             m1, m2, m3
    mova                 m2, [o(deintq_perm)]
    vpbroadcastd         m3, [o(pixel_clip)]
    REPX       {psrad x, 6}, m4, m5, m6, m7
    call .write_16x4
    packssdw             m0, m4, m5
    packssdw             m1, m6, m7
    REPX       {psrad x, 6}, m8, m9, m10, m11
    call .write_16x4
    packssdw             m0, m8, m9
    packssdw             m1, m10, m11
.pass2_end2:
    REPX       {psrad x, 6}, m12, m13, m14, m15
    call .write_16x4
    packssdw             m0, m12, m13
    packssdw             m1, m14, m15
    call .write_16x4
    RET
ALIGN function_align
.write_16x4:
    mova               ym16, [dstq+strideq*0]
    vinserti32x8        m16, [dstq+strideq*1], 1
    mova               ym17, [dstq+strideq*2]
    vinserti32x8        m17, [dstq+r6       ], 1
    vpermq               m0, m2, m0
    vpermq               m1, m2, m1
    paddsw              m16, m0
    paddsw              m17, m1
    psubusw             m16, m3
    psubusw             m17, m3
    mova          [dstq+strideq*0], ym16
    vextracti32x8 [dstq+strideq*1], m16, 1
    mova          [dstq+strideq*2], ym17
    vextracti32x8 [dstq+r6       ], m17, 1
    lea                dstq, [dstq+strideq*4]
    ret
ALIGN function_align
    IDCT16_PART1
    ret
ALIGN function_align
.main_part2:
    IDCT16_PART2
    ret

%macro IADST16_PART1 0
%if mmsize == 64
.main_part1_fast:
%endif
    pmulld              m15, m0, [o(pd_16364)] {bcstd} ; t1
    pmulld               m0, [o(pd_804)] {bcstd}       ; t0
    pmulld              m13, m2, [o(pd_15893)] {bcstd} ; t3
    pmulld               m2, [o(pd_3981)] {bcstd}      ; t2
    pmulld              m11, m4, [o(pd_14811)] {bcstd} ; t5
    pmulld               m4, [o(pd_7005)] {bcstd}      ; t4
    pmulld               m9, m6, [o(pd_13160)] {bcstd} ; t7
    pmulld               m6, [o(pd_9760)] {bcstd}      ; t6
    pmulld               m8, m7, [o(pd_11003)] {bcstd} ; t8
    pmulld               m7, [o(pd_12140)] {bcstd}     ; t9
    pmulld              m10, m5, [o(pd_8423)] {bcstd}  ; t10
    pmulld               m5, [o(pd_14053)] {bcstd}     ; t11
    pmulld              m12, m3, [o(pd_5520)] {bcstd}  ; t12
    pmulld               m3, [o(pd_15426)] {bcstd}     ; t13
    pmulld              m14, m1, [o(pd_2404)] {bcstd}  ; t14
    pmulld               m1, [o(pd_16207)] {bcstd}     ; t15
    REPX  {psubd x, m20, x}, m15, m13, m11, m9
%if mmsize == 64 ; for the ymm variant we only ever use the fast path
    jmp %%main_part1b
ALIGN function_align
.main_part1:
    ITX_MULSUB_2D        15,  0, 16, 17, 18, _,   804, 16364 ; t1,  t0
    ITX_MULSUB_2D        13,  2, 16, 17, 18, _,  3981, 15893 ; t3,  t2
    ITX_MULSUB_2D        11,  4, 16, 17, 18, _,  7005, 14811 ; t5,  t4
    ITX_MULSUB_2D         9,  6, 16, 17, 18, _,  9760, 13160 ; t7,  t6
    ITX_MULSUB_2D         7,  8, 16, 17, 18, _, 12140, 11003 ; t9,  t8
    ITX_MULSUB_2D         5, 10, 16, 17, 18, _, 14053,  8423 ; t11, t10
    ITX_MULSUB_2D         3, 12, 16, 17, 18, _, 15426,  5520 ; t13, t12
    ITX_MULSUB_2D         1, 14, 16, 17, 18, _, 16207,  2404 ; t15, t14
    REPX     {paddd x, m20}, m15, m13, m11, m9
%%main_part1b:
%endif
    REPX     {paddd x, m20}, m0, m2, m4, m6
    psubd               m16, m2, m10  ; t10a
    paddd                m2, m10      ; t2a
    psubd               m10, m9, m1   ; t15a
    paddd                m9, m1       ; t7a
    psubd                m1, m13, m5  ; t11a
    paddd               m13, m5       ; t3a
    psubd                m5, m6, m14  ; t14a
    paddd                m6, m14      ; t6a
    REPX      {psrad x, 14}, m16, m10, m1, m5
    psubd               m14, m0, m8   ; t8a
    paddd                m0, m8       ; t0a
    psubd                m8, m15, m7  ; t9a
    paddd               m15, m7       ; t1a
    psubd                m7, m4, m12  ; t12a
    paddd                m4, m12      ; t4a
    paddd               m12, m11, m3  ; t5a
    psubd               m11, m3       ; t13a
    REPX      {psrad x, 14}, m14, m8, m7, m11
    vpbroadcastd        m19, [o(pd_9102)]
    vpbroadcastd        m18, [o(pd_13623)]
    ITX_MULSUB_2D        16, 1, 3, 17, _, _, 18, 19 ; t11, t10
    ITX_MULSUB_2D        10, 5, 3, 17, _, _, 19, 18 ; t14, t15
    vpbroadcastd        m19, [o(pd_16069)]
    vpbroadcastd        m18, [o(pd_3196)]
    ITX_MULSUB_2D        14, 8, 3, 17, _, _, 18, 19 ; t9,  t8
    ITX_MULSUB_2D        11, 7, 3, 17, _, _, 19, 18 ; t12, t13
    vpbroadcastd        m19, [o(pd_6270)]
    vpbroadcastd        m18, [o(pd_15137)]
    REPX      {psrad x, 14}, m15, m12, m0, m4
    psubd                m3, m15, m12 ; t5
    paddd               m15, m12      ; t1
    psubd               m12, m0, m4   ; t4
    paddd                m0, m4       ; t0
    REPX      {psrad x, 14}, m2, m6, m13, m9
    psubd                m4, m2, m6   ; t6
    paddd                m2, m6       ; t2
    psubd                m6, m13, m9  ; t7
    paddd                m9, m13      ; t3
    REPX     {paddd x, m20}, m8, m14, m1, m16
    psubd               m13, m8, m11  ; t12a
    paddd                m8, m11      ; t8a
    psubd               m11, m14, m7  ; t13a
    paddd               m14, m7       ; t9a
    psubd                m7, m1, m10  ; t14a
    paddd                m1, m10      ; t10a
    psubd               m10, m16, m5  ; t15a
    paddd               m16, m5       ; t11a
    REPX      {psrad x, 14}, m13, m11, m7, m10
    ITX_MULSUB_2D        12,  3, 5, 17, _, _, 19, 18 ; t5a, t4a
    ITX_MULSUB_2D         6,  4, 5, 17, _, _, 18, 19 ; t6a, t7a
    ITX_MULSUB_2D        13, 11, 5, 17, _, _, 19, 18 ; t13, t12
    ITX_MULSUB_2D        10,  7, 5, 17, _, _, 18, 19 ; t14, t15
    REPX      {psrad x, 14}, m8, m1, m14, m16
    psubd                m5, m8, m1   ;  t10
    paddd                m1, m8       ; -out1
    psubd                m8, m15, m9  ;  t3a
    paddd               m15, m9       ; -out15
    psubd                m9, m14, m16 ;  t11
    paddd               m14, m16      ;  out14
    psubd               m16, m0, m2   ;  t2a
    paddd                m0, m2       ;  out0
    REPX     {paddd x, m20}, m11, m13, m12, m3
    paddd                m2, m11, m10 ;  out2
    psubd               m11, m10      ;  t14a
    psubd               m10, m13, m7  ;  t15a
    paddd               m13, m7       ; -out13
    psubd                m7, m12, m4  ;  t7
    paddd               m12, m4       ;  out12
    psubd                m4, m3, m6   ;  t6
    paddd                m3, m6       ; -out3
    REPX      {psrad x, 14}, m10, m7, m11, m4
    REPX    {pmulld x, m21}, m9, m10, m7, m8, m5, m11, m4, m16
    REPX      {psrad x, 14}, m2, m13, m12, m3
%endmacro

%macro IADST16_PART2 0
    paddd                m9, m20
    psubd               m10, m20, m10
    paddd                m7, m20
    psubd                m8, m20, m8
    paddd                m6, m9, m5   ; out6
    psubd                m9, m5       ; out9
    psubd                m5, m10, m11 ; out5
    paddd               m10, m11      ; out10
    psubd               m11, m7, m4   ; out11
    paddd                m4, m7       ; out4
    psubd                m7, m8, m16  ; out7
    paddd                m8, m16      ; out8
%endmacro

%macro IADST16_PASS1_END 0
    pxor                m16, m16
    psubd                m1, m16, m1
    psubd                m3, m16, m3
    psubd               m13, m16, m13
    psubd               m15, m16, m15
    REPX      {psrad x, 14}, m4, m5, m6, m7, m8, m9, m10, m11
%endmacro

INV_TXFM_16X16_FN adst, dct, 39-18
INV_TXFM_16X16_FN adst, adst

cglobal vp9_iadst_16x16_internal_10, 0, 7, 22, dst, stride, c, eob, tx2
    mova                 m0, [cq+64* 0]
    mova                 m1, [cq+64* 1]
    mova                 m2, [cq+64* 2]
    mova                 m3, [cq+64* 3]
    mova                 m4, [cq+64* 4]
    mova                 m5, [cq+64* 5]
    mova                 m6, [cq+64* 6]
    mova                 m7, [cq+64* 7]
    vpbroadcastd        m20, [o(pd_8192)]
    vpbroadcastd        m21, [o(pd_11585)]
    sub                eobd, 39
    jl .pass1_fast
    mova                 m8, [cq+64* 8]
    mova                 m9, [cq+64* 9]
    mova                m10, [cq+64*10]
    mova                m11, [cq+64*11]
    mova                m12, [cq+64*12]
    mova                m13, [cq+64*13]
    mova                m14, [cq+64*14]
    mova                m15, [cq+64*15]
    call .main_part1
    call .main_part2
    IADST16_PASS1_END
    jmp m(vp9_idct_16x16_internal_10).pass1_end
.pass1_fast:
    WRAP_YMM IADST16_PART1
    WRAP_YMM IADST16_PART2
    WRAP_YMM IADST16_PASS1_END
    jmp m(vp9_idct_16x16_internal_10).pass1_fast_end
.pass2:
    test               eobd, eobd
    jl .pass2_fast
    call .main_part1
    jmp .pass2_end
.pass2_fast:
    call .main_part1_fast
.pass2_end:
    vpbroadcastd        m20, [o(pd_532480)]
    call .main_part2
    vpbroadcastd        m16, [o(pixel_clip6)]
    REPX     {paddd x, m16}, m0, m2, m12, m14
    REPX  {psubd x, m16, x}, m1, m3, m13, m15
    REPX       {psrad x, 6}, m0, m1, m2, m3
    packssdw             m0, m1
    lea                  r6, [strideq*3]
    packssdw             m1, m2, m3
    mova                 m2, [o(deintq_perm)]
    vpbroadcastd         m3, [o(pixel_clip)]
    REPX      {psrad x, 20}, m4, m5, m6, m7
    call m(vp9_idct_16x16_internal_10).write_16x4
    packssdw             m0, m4, m5
    packssdw             m1, m6, m7
    paddsw               m0, m3
    paddsw               m1, m3
    REPX      {psrad x, 20}, m8, m9, m10, m11
    call m(vp9_idct_16x16_internal_10).write_16x4
    packssdw             m0, m8, m9
    packssdw             m1, m10, m11
    paddsw               m0, m3
    paddsw               m1, m3
    jmp m(vp9_idct_16x16_internal_10).pass2_end2
ALIGN function_align
    IADST16_PART1
    ret
ALIGN function_align
.main_part2:
    IADST16_PART2
    ret

cglobal vp9_idct_idct_32x32_add_10, 4, 7, 23, 64*64, dst, stride, c, eob
%undef cmp
    lea                  r5, [o_base]
    dec                eobd
    jnz .pass1
    imul                r6d, [cq], 11585
    vpbroadcastd         m3, [o(pixel_clip)]
    mov                [cq], r3d
    add                 r6d, 8192
    sar                 r6d, 14
    imul                r6d, 11585
    or                  r3d, 16
    add                 r6d, 532480
    sar                 r6d, 20
    vpbroadcastw         m2, r6d
    paddsw               m2, m3
.dconly_loop:
    paddsw               m0, m2, [dstq+strideq*0]
    paddsw               m1, m2, [dstq+strideq*1]
    psubusw              m0, m3
    psubusw              m1, m3
    mova   [dstq+strideq*0], m0
    mova   [dstq+strideq*1], m1
    lea                dstq, [dstq+strideq*2]
    dec                 r3d
    jg .dconly_loop
    RET
.pass1:
    vpbroadcastd        m20, [o(pd_8192)]
    vpbroadcastd        m21, [o(pd_11585)]
    cmp                eobd, 135
    jl .pass1_fast
    add                  cq, 64
    lea                  r4, [rsp+64*8]
    cmp                eobd, 579
    jl .pass1_right_fast
    mov                 r6d, 128*28
    call .pass1_main
    jmp .pass1_right_end
.pass1_right_fast: ; bottomright quadrant is zero
    mova                 m0, [cq+128* 1]
    mova                 m1, [cq+128* 3]
    mova                 m2, [cq+128* 5]
    mova                 m3, [cq+128* 7]
    mova                 m4, [cq+128* 9]
    mova                 m5, [cq+128*11]
    mova                 m6, [cq+128*13]
    mova                 m7, [cq+128*15]
    call .main_fast
    mova                 m0, [cq+128* 0]
    mova                 m1, [cq+128* 2]
    mova                 m2, [cq+128* 4]
    mova                 m3, [cq+128* 6]
    mova                 m4, [cq+128* 8]
    mova                 m5, [cq+128*10]
    mova                 m6, [cq+128*12]
    mova                 m7, [cq+128*14]
    call m(vp9_idct_16x16_internal_10).main_part1_fast
    mov                 r6d, 128*12
    call .pass1_main_end
.pass1_right_end:
    mova         [r4+64* 8], m0
    mova         [r4+64* 9], m1
    mova         [r4+64*10], m2
    mova         [r4+64*11], m3
    mova         [r4+64*12], m4
    mova         [r4+64*13], m5
    mova         [r4+64*14], m6
    mova         [r4+64*15], m7
    mova         [r4+64*16], m16
    mova         [r4+64*17], m17
    mova         [r4+64*18], m18
    mova         [r4+64*19], m19
    mova         [r4+64*20], m8
    mova         [r4+64*21], m9
    mova         [r4+64*22], m10
    mova         [r4+64*23], m11
    sub                  cq, 64
    sub                  r4, 64*8
    mov                 r6d, 128*28
    call .pass1_main
    mova                m12, [r4+64*20]
    mova                m13, [r4+64*21]
    mova                m14, [r4+64*22]
    mova                m15, [r4+64*23]
    mova         [r4+64*20], m8
    mova         [r4+64*21], m9
    mova         [r4+64*22], m10
    mova         [r4+64*23], m11
    mova                 m8, [r4+64*16]
    mova                 m9, [r4+64*17]
    mova                m10, [r4+64*18]
    mova                m11, [r4+64*19]
    mova         [r4+64*16], m16
    mova         [r4+64*17], m17
    mova         [r4+64*18], m18
    mova         [r4+64*19], m19
    call .main
    mova                 m0, [r4+64*16]
    mova                 m1, [r4+64*17]
    mova                 m2, [r4+64*18]
    mova                 m3, [r4+64*19]
    mova                 m4, [r4+64*20]
    mova                 m5, [r4+64*21]
    mova                 m6, [r4+64*22]
    mova                 m7, [r4+64*23]
    mova                 m8, [r4+64*24]
    mova                 m9, [r4+64*25]
    mova                m10, [r4+64*26]
    mova                m11, [r4+64*27]
    mova                m12, [r4+64*28]
    mova                m13, [r4+64*29]
    mova                m14, [r4+64*30]
    mova                m15, [r4+64*31]
    call m(vp9_idct_16x16_internal_10).main_part1
    call .pass2_main_left
    mova                 m8, [r4+64* 8]
    mova                 m9, [r4+64* 9]
    mova                m10, [r4+64*10]
    mova                m11, [r4+64*11]
    mova                m12, [r4+64*12]
    mova                m13, [r4+64*13]
    mova                m14, [r4+64*14]
    mova                m15, [r4+64*15]
    TRANSPOSE_4DQ         8, 10, 12, 14, 16
    TRANSPOSE_4DQ         9, 11, 13, 15, 16
    call .main
    call .pass2_main_right
    mova                 m8, [r4+64*24]
    mova                 m9, [r4+64*25]
    mova                m10, [r4+64*26]
    mova                m11, [r4+64*27]
    mova                m12, [r4+64*28]
    mova                m13, [r4+64*29]
    mova                m14, [r4+64*30]
    mova                m15, [r4+64*31]
    TRANSPOSE_4DQ         8, 10, 12, 14, 16
    TRANSPOSE_4DQ         9, 11, 13, 15, 16
    call m(vp9_idct_16x16_internal_10).main_part1
    jmp .pass2_end
.pass1_fast:
    mova                 m0, [cq+128* 1]
    mova                 m1, [cq+128* 3]
    mova                 m2, [cq+128* 5]
    mova                 m3, [cq+128* 7]
    mova                 m4, [cq+128* 9]
    mova                 m5, [cq+128*11]
    mova                 m6, [cq+128*13]
    mova                 m7, [cq+128*15]
    mov                  r4, rsp
    call .main_fast
    mova                 m0, [cq+128* 0]
    mova                 m1, [cq+128* 2]
    mova                 m2, [cq+128* 4]
    mova                 m3, [cq+128* 6]
    mova                 m4, [cq+128* 8]
    mova                 m5, [cq+128*10]
    mova                 m6, [cq+128*12]
    mova                 m7, [cq+128*14]
    call m(vp9_idct_16x16_internal_10).main_part1_fast
    call m(vp9_idct_16x16_internal_10).main_part2
    mov                 r6d, 128*12
    call .pass1_main_end2
    mova         [r4+64*16], m16
    mova         [r4+64*17], m17
    mova         [r4+64*18], m18
    mova         [r4+64*19], m19
    mova         [r4+64*20], m8
    mova         [r4+64*21], m9
    mova         [r4+64*22], m10
    mova         [r4+64*23], m11
    call .main_fast
    mova                 m0, [r4+64*16]
    mova                 m1, [r4+64*17]
    mova                 m2, [r4+64*18]
    mova                 m3, [r4+64*19]
    mova                 m4, [r4+64*20]
    mova                 m5, [r4+64*21]
    mova                 m6, [r4+64*22]
    mova                 m7, [r4+64*23]
    call m(vp9_idct_16x16_internal_10).main_part1_fast
    call .pass2_main_left
    call .main_fast
    call .pass2_main_right
    call m(vp9_idct_16x16_internal_10).main_part1_fast
.pass2_end:
    paddd                m0, m22
    paddd               m18, m22
    call m(vp9_idct_16x16_internal_10).main_part2
    mova                m20, [o(deintq_perm)]
    rorx                 r2, strideq, 59 ; strideq*32
    vpbroadcastd        m21, [o(pixel_clip)]
    add                  r2, dstq
%assign i 0
%rep 16
    mova                m16, [r4+64*(15-i)]
    mova                m17, [r4+64*(i-16)]
    mova                m18, [r4-64*(17+i)]
    paddd               m19, m %+ i, m16
    psubd                m0, m %+ i, m16
    call .write_32x2
    %assign i i+1
%endrep
    RET
ALIGN function_align
.write_32x2:
    paddd               m16, m17, m18
    psubd               m17, m18
    REPX       {psrad x, 6}, m19, m16, m0, m17
    packssdw            m16, m19
    packssdw            m17, m0
    sub                  r2, strideq
    vpermq              m16, m20, m16
    vpermq              m17, m20, m17
    paddsw              m16, [dstq]
    paddsw              m17, [r2  ]
    psubusw             m16, m21
    psubusw             m17, m21
    mova             [dstq], m16
    mova             [r2  ], m17
    add                dstq, strideq
    ret
ALIGN function_align
.pass1_main:
    mova                 m0, [cq+128* 1]
    mova                 m1, [cq+128* 3]
    mova                 m2, [cq+128* 5]
    mova                 m3, [cq+128* 7]
    mova                 m4, [cq+128* 9]
    mova                 m5, [cq+128*11]
    mova                 m6, [cq+128*13]
    mova                 m7, [cq+128*15]
    mova                 m8, [cq+128*17]
    mova                 m9, [cq+128*19]
    mova                m10, [cq+128*21]
    mova                m11, [cq+128*23]
    mova                m12, [cq+128*25]
    mova                m13, [cq+128*27]
    mova                m14, [cq+128*29]
    mova                m15, [cq+128*31]
    call .main
    mova                 m0, [cq+128* 0]
    mova                 m1, [cq+128* 2]
    mova                 m2, [cq+128* 4]
    mova                 m3, [cq+128* 6]
    mova                 m4, [cq+128* 8]
    mova                 m5, [cq+128*10]
    mova                 m6, [cq+128*12]
    mova                 m7, [cq+128*14]
    mova                 m8, [cq+128*16]
    mova                 m9, [cq+128*18]
    mova                m10, [cq+128*20]
    mova                m11, [cq+128*22]
    mova                m12, [cq+128*24]
    mova                m13, [cq+128*26]
    mova                m14, [cq+128*28]
    mova                m15, [cq+128*30]
    call m(vp9_idct_16x16_internal_10).main_part1
.pass1_main_end:
    call m(vp9_idct_16x16_internal_10).main_part2
.pass1_main_end2:
    pxor                m16, m16
.pass1_zero_loop:
    mova      [cq+r6+128*0], m16
    mova      [cq+r6+128*1], m16
    mova      [cq+r6+128*2], m16
    mova      [cq+r6+128*3], m16
    sub                 r6d, 128*4
    jge .pass1_zero_loop
    mova                m16, [r4+64*15]
    mova                m19, [r4+64*14]
    mova                m22, [r4+64*13]
    mova                m17, [r4+64*12]
    psubd               m18, m0, m16
    paddd               m16, m0
    paddd                m0, m19, m1
    psubd               m19, m1, m19
    paddd                m1, m17, m3
    psubd                m3, m17
    paddd               m17, m2, m22
    psubd                m2, m22
    TRANSPOSE_4D          3,  2, 19, 18, 22 ; 28 29 30 31
    TRANSPOSE_4D         16,  0, 17,  1, 22 ;  0  1  2  3
    mova         [r4+64*54], m3
    mova         [r4+64*55], m19
    mova         [r4+64*38], m2
    mova         [r4+64*39], m18
    mova                 m2, [r4+64*11]
    mova                m19, [r4+64*10]
    mova                 m3, [r4+64* 9]
    mova                m22, [r4+64* 8]
    paddd               m18, m4, m2
    psubd                m4, m2
    paddd                m2, m5, m19
    psubd                m5, m19
    paddd               m19, m6, m3
    psubd                m6, m3
    paddd                m3, m7, m22
    psubd                m7, m22
    TRANSPOSE_4D          7,  6,  5,  4, 22 ; 24 25 26 27
    TRANSPOSE_4D         18,  2, 19,  3, 22 ;  4  5  6  7
    mova         [r4+64*52], m7
    mova         [r4+64*53], m5
    mova         [r4+64*36], m6
    mova         [r4+64*37], m4
    mova                 m7, [r4+64* 7]
    mova                 m4, [r4+64* 6]
    mova                 m5, [r4+64* 5]
    mova                m22, [r4+64* 4]
    psubd                m6, m8, m7
    paddd                m8, m7
    psubd                m7, m9, m4
    paddd                m4, m9
    paddd                m9, m10, m5
    psubd               m10, m5
    paddd                m5, m11, m22
    psubd               m11, m22
    TRANSPOSE_4D         11, 10,  7,  6, 22 ; 20 21 22 23
    TRANSPOSE_4D          8,  4,  9,  5, 22 ;  8  9 10 11
    mova         [r4+64*50], m11
    mova         [r4+64*51], m7
    mova         [r4+64*34], m10
    mova         [r4+64*35], m6
    mova                 m6, [r4+64* 3]
    mova                m11, [r4+64* 2]
    mova                 m7, [r4+64* 1]
    mova                m22, [r4+64* 0]
    paddd               m10, m12, m6
    psubd               m12, m6
    paddd                m6, m13, m11
    psubd               m13, m11
    paddd               m11, m14, m7
    psubd               m14, m7
    paddd                m7, m15, m22
    psubd               m15, m22
    TRANSPOSE_4D         15, 14, 13, 12, 22 ; 16 17 18 19
    TRANSPOSE_4D         10,  6, 11,  7, 22 ; 12 13 14 15
    mova         [r4+64*48], m15
    mova         [r4+64*49], m13
    mova         [r4+64*32], m14
    mova         [r4+64*33], m12
    TRANSPOSE_4DQ         0,  2,  4,  6, 22
    TRANSPOSE_4DQ         1,  3,  5,  7, 22
    TRANSPOSE_4DQ        16, 18,  8, 10, 22
    TRANSPOSE_4DQ        17, 19,  9, 11, 22
    ret
ALIGN function_align
.pass2_main_left:
    vpbroadcastd        m22, [o(pixel_clip6)]
    paddd                m0, m22
    paddd               m18, m22
    call m(vp9_idct_16x16_internal_10).main_part2
    mova         [r4+64*16], m0
    mova         [r4+64*17], m1
    mova         [r4+64*18], m2
    mova         [r4+64*19], m3
    mova         [r4+64*20], m4
    mova         [r4+64*21], m5
    mova         [r4+64*22], m6
    mova         [r4+64*23], m7
    mova         [r4+64*24], m8
    mova         [r4+64*25], m9
    mova         [r4+64*26], m10
    mova         [r4+64*27], m11
    mova         [r4+64*28], m12
    mova         [r4+64*29], m13
    mova         [r4+64*30], m14
    mova         [r4+64*31], m15
    add                  r4, 64*32
    mova                 m0, [r4+64* 0]
    mova                 m1, [r4+64* 1]
    mova                 m2, [r4+64* 2]
    mova                 m3, [r4+64* 3]
    mova                 m4, [r4+64* 4]
    mova                 m5, [r4+64* 5]
    mova                 m6, [r4+64* 6]
    mova                 m7, [r4+64* 7]
    jmp .pass2_main_transpose
ALIGN function_align
.pass2_main_right:
    mova                 m0, [r4+64*16]
    mova                 m1, [r4+64*17]
    mova                 m2, [r4+64*18]
    mova                 m3, [r4+64*19]
    mova                 m4, [r4+64*20]
    mova                 m5, [r4+64*21]
    mova                 m6, [r4+64*22]
    mova                 m7, [r4+64*23]
.pass2_main_transpose:
    TRANSPOSE_4DQ         0, 2, 4, 6, 8
    TRANSPOSE_4DQ         1, 3, 5, 7, 8
    ret
ALIGN function_align
.main_fast:
    pmulld              m15, m0, [o(pd_16364)] {1to16} ; t31a
    pmulld               m0, [o(pd_804)] {1to16}       ; t16a
    pmulld               m8, m7, [o(pd_11003)] {1to16} ; t17a
    pmulld               m7, [o(pd_12140)] {1to16}     ; t30a
    pmulld              m11, m4, [o(pd_14811)] {1to16} ; t29a
    pmulld               m4, [o(pd_7005)] {1to16}      ; t18a
    pmulld              m12, m3, [o(pd_5520)] {1to16}  ; t19a
    pmulld               m3, [o(pd_15426)] {1to16}     ; t28a
    pmulld              m13, m2, [o(pd_15893)] {1to16} ; t27a
    pmulld               m2, [o(pd_3981)] {1to16}      ; t20a
    pmulld              m10, m5, [o(pd_8423)] {1to16}  ; t21a
    pmulld               m5, [o(pd_14053)] {1to16}     ; t26a
    pmulld               m9, m6, [o(pd_13160)] {1to16} ; t25a
    pmulld               m6, [o(pd_9760)] {1to16}      ; t22a
    pmulld              m14, m1, [o(pd_2404)] {1to16}  ; t23a
    pmulld               m1, [o(pd_16207)] {1to16}     ; t24a
    REPX  {psubd x, m20, x}, m8, m12, m10, m14
    jmp .main2
ALIGN function_align
.main:
    ITX_MULSUB_2D         0, 15, 16, 17, 18, _,   804, 16364 ; t16a, t31a
    ITX_MULSUB_2D         8,  7, 16, 17, 18, _, 12140, 11003 ; t17a, t30a
    ITX_MULSUB_2D         4, 11, 16, 17, 18, _,  7005, 14811 ; t18a, t29a
    ITX_MULSUB_2D        12,  3, 16, 17, 18, _, 15426,  5520 ; t19a, t28a
    ITX_MULSUB_2D         2, 13, 16, 17, 18, _,  3981, 15893 ; t20a, t27a
    ITX_MULSUB_2D        10,  5, 16, 17, 18, _, 14053,  8423 ; t21a, t26a
    ITX_MULSUB_2D         6,  9, 16, 17, 18, _,  9760, 13160 ; t22a, t25a
    ITX_MULSUB_2D        14,  1, 16, 17, 18, _, 16207,  2404 ; t23a, t24a
    REPX     {paddd x, m20}, m8, m12, m10, m14
.main2:
    REPX     {paddd x, m20}, m0, m15, m7, m4, m3, m11
    REPX     {psrad x, 14 }, m8, m0, m15, m7, m12, m4, m3, m11
    psubd               m16, m0, m8   ; t17
    paddd                m0, m8       ; t16
    psubd                m8, m15, m7  ; t30
    paddd               m15, m7       ; t31
    paddd                m7, m12, m4  ; t19
    psubd               m12, m4       ; t18
    paddd                m4, m3, m11  ; t28
    psubd                m3, m11      ; t29
    REPX     {paddd x, m20}, m2, m13, m5, m6, m1, m9
    REPX     {psrad x, 14 }, m10, m2, m13, m5, m14, m6, m1, m9
    psubd               m11, m2, m10  ; t21
    paddd                m2, m10      ; t20
    psubd               m10, m13, m5  ; t26
    paddd               m13, m5       ; t27
    psubd                m5, m14, m6  ; t22
    paddd                m6, m14      ; t23
    psubd               m14, m1, m9   ; t25
    paddd                m9, m1       ; t24
    vpbroadcastd        m19, [o(pd_16069)]
    vpbroadcastd        m18, [o(pd_3196)]
    ITX_MULSUB_2D         8, 16,  1, 17, _, 20, 18, 19    ; t17a, t30a
    ITX_MULSUB_2D         3, 12,  1, 17, _, 20, 18, 19, 1 ; t29a, t18a
    vpbroadcastd        m19, [o(pd_9102)]
    vpbroadcastd        m18, [o(pd_13623)]
    ITX_MULSUB_2D        10, 11,  1, 17, _, 20, 18, 19    ; t21a, t26a
    ITX_MULSUB_2D        14,  5,  1, 17, _, 20, 18, 19, 1 ; t25a, t22a
    paddd                m1, m6, m2   ; t23a
    psubd                m6, m2       ; t20a
    psubd                m2, m9, m13  ; t27a
    paddd                m9, m13      ; t24a
    psubd               m13, m15, m4  ; t28a
    paddd               m15, m4       ; t31a
    psubd                m4, m8, m12  ; t18
    paddd                m8, m12      ; t17
    psubd               m12, m0, m7   ; t19a
    paddd                m0, m7       ; t16a
    psubd                m7, m16, m3  ; t29
    paddd                m3, m16      ; t30
    paddd               m16, m5, m10  ; t22
    psubd                m5, m10      ; t21
    psubd               m10, m14, m11 ; t26
    paddd               m14, m11      ; t25
    vpbroadcastd        m19, [o(pd_15137)]
    vpbroadcastd        m18, [o(pd_6270)]
    ITX_MULSUB_2D        13, 12, 11, 17, _, 20, 18, 19    ; t19,  t28
    ITX_MULSUB_2D         2,  6, 11, 17, _, 20, 18, 19, 1 ; t27,  t20
    ITX_MULSUB_2D         7,  4, 11, 17, _, 20, 18, 19    ; t18a, t29a
    ITX_MULSUB_2D        10,  5, 11, 17, _, 20, 18, 19, 1 ; t26a, t21a
    psubd               m11, m0, m1   ; t23
    paddd                m0, m1       ; t16
    paddd                m1, m16, m8  ; t17a
    psubd               m16, m8, m16  ; t22a
    psubd                m8, m15, m9  ; t24
    paddd               m15, m9       ; t31
    psubd                m9, m3, m14  ; t25a
    paddd               m14, m3       ; t30a
    paddd                m3, m6, m13  ; t19a
    psubd                m6, m13, m6  ; t20a
    paddd               m13, m10, m4  ; t29
    psubd               m10, m4, m10  ; t26
    psubd                m4, m12, m2  ; t27a
    paddd               m12, m2       ; t28a
    paddd                m2, m7, m5   ; t18
    psubd                m7, m5       ; t21
    REPX    {pmulld x, m21}, m10, m8, m4, m9, m7, m11, m6, m16
    mova         [r4+64* 0], m0
    mova         [r4+64* 1], m1
    mova         [r4+64* 2], m2
    mova         [r4+64* 3], m3
    mova         [r4+64*12], m12
    mova         [r4+64*13], m13
    mova         [r4+64*14], m14
    mova         [r4+64*15], m15
    REPX    {paddd  x, m20}, m10, m8, m4, m9
    psubd                m5, m10, m7  ; t21a
    paddd               m10, m7       ; t26a
    psubd                m7, m8, m11  ; t23a
    paddd                m8, m11      ; t24a
    REPX    {psrad  x, 14 }, m5, m10, m7, m8
    paddd               m11, m4, m6   ; t27
    psubd                m4, m6       ; t20
    psubd                m6, m9, m16  ; t22
    paddd                m9, m16      ; t25
    REPX    {psrad  x, 14 }, m11, m4, m6, m9
    mova         [r4+64* 4], m4
    mova         [r4+64* 5], m5
    mova         [r4+64* 6], m6
    mova         [r4+64* 7], m7
    mova         [r4+64* 8], m8
    mova         [r4+64* 9], m9
    mova         [r4+64*10], m10
    mova         [r4+64*11], m11
    ret

%endif
