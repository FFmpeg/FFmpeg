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

dup16_perm:  db  0,  1,  0,  1,  2,  3,  2,  3,  4,  5,  4,  5,  6,  7,  6,  7
             db  8,  9,  8,  9, 10, 11, 10, 11, 12, 13, 12, 13, 14, 15, 14, 15
             db 16, 17, 16, 17, 18, 19, 18, 19, 20, 21, 20, 21, 22, 23, 22, 23
             db 24, 25, 24, 25, 26, 27, 26, 27, 28, 29, 28, 29, 30, 31, 30, 31
itx_perm:    dq 0x0000000820150440, 0x0000000231372604
             dq 0x0000000ca8041551, 0x00000006b9263715
             dq 0x00000001ec9d8c62, 0x0000000bfdbfae26
             dq 0x00000005648c9d73, 0x0000000f75aebf37
deint_shuf:  db  0,  1,  4,  5,  8,  9, 12, 13,  2,  3,  6,  7, 10, 11, 14, 15
int_shuf1:   db  0,  1,  8,  9,  2,  3, 10, 11,  4,  5, 12, 13,  6,  7, 14, 15
int_shuf2:   db  8,  9,  0,  1, 10, 11,  2,  3, 12, 13,  4,  5, 14, 15,  6,  7
pw_512:      times 4 dw  512
pw_m512:     times 4 dw -512
pw_15137_6270x2x4:   times 4 dw  15137*2
                     times 4 dw   6270*2
pw_11585_m11585x2x4: times 4 dw  11585*2
pw_m11585_11585x2x4: times 4 dw -11585*2
pw_11585_11585x2:    times 4 dw  11585*2
int_mshift:  db 142, 150, 0, 0, 174, 182, 0, 0
pd_8192:     dd 8192
pw_804x2:    times 2 dw    804*2
pw_1606x2:   times 2 dw   1606*2
pw_3196x2:   times 2 dw   3196*2
pw_3981x2:   times 2 dw   3981*2
pw_6270x2:   times 2 dw   6270*2
pw_7005x2:   times 2 dw   7005*2
pw_7723x2:   times 2 dw   7723*2
pw_9760x2:   times 2 dw   9760*2
pw_12140x2:  times 2 dw  12140*2
pw_12665x2:  times 2 dw  12665*2
pw_13160x2:  times 2 dw  13160*2
pw_13623x2:  times 2 dw  13623*2
pw_14053x2:  times 2 dw  14053*2
pw_14449x2:  times 2 dw  14449*2
pw_14811x2:  times 2 dw  14811*2
pw_15137x2:  times 2 dw  15137*2
pw_15426x2:  times 2 dw  15426*2
pw_15679x2:  times 2 dw  15679*2
pw_15893x2:  times 2 dw  15893*2
pw_16069x2:  times 2 dw  16069*2
pw_16207x2:  times 2 dw  16207*2
pw_16305x2:  times 2 dw  16305*2
pw_16364x2:  times 2 dw  16364*2
pw_m2404x2:  times 2 dw  -2404*2
pw_m4756x2:  times 2 dw  -4756*2
pw_m5520x2:  times 2 dw  -5520*2
pw_m8423x2:  times 2 dw  -8423*2
pw_m9102x2:  times 2 dw  -9102*2
pw_m10394x2: times 2 dw -10394*2
pw_m11003x2: times 2 dw -11003*2
pw_804_16364x2:    dw    804*2, 16364*2
pw_1606_16305x2:   dw   1606*2, 16305*2
pw_3196_16069x2:   dw   3196*2, 16069*2
pw_3981_15893x2:   dw   3981*2, 15893*2
pw_7005_14811x2:   dw   7005*2, 14811*2
pw_7723_14449x2:   dw   7723*2, 14449*2
pw_9760_13160x2:   dw   9760*2, 13160*2
pw_m2404_16207x2:  dw  -2404*2, 16207*2
pw_m4756_15679x2:  dw  -4756*2, 15679*2
pw_m5520_15426x2:  dw  -5520*2, 15426*2
pw_m8423_14053x2:  dw  -8423*2, 14053*2
pw_m9102_13623x2:  dw  -9102*2, 13623*2
pw_m10394_12665x2: dw -10394*2, 12665*2
pw_m11003_12140x2: dw -11003*2, 12140*2

%macro COEF_PAIR 2-3 0
%if %3 & 4
pw_%1_m%2:  dw  %1, -%2
%else
pw_%1_%2:   dw  %1,  %2
%if %3 & 2
pw_m%1_%2:  dw -%1,  %2
%else
pw_m%2_%1:  dw -%2,  %1
%endif
%endif
%if %3 & 1
pw_m%1_m%2: dw -%1, -%2
%endif
%endmacro

COEF_PAIR   804, 16364
COEF_PAIR  1606, 16305
COEF_PAIR  3196, 16069, 1
COEF_PAIR  3981, 15893
COEF_PAIR  6270, 15137, 1
COEF_PAIR  7005, 14811
COEF_PAIR  7723, 14449
COEF_PAIR  9102, 13623
COEF_PAIR  9760, 13160
COEF_PAIR 11585, 11585, 1
COEF_PAIR 12140, 11003
COEF_PAIR 12665, 10394
COEF_PAIR 13623,  9102, 1
COEF_PAIR 14053,  8423
COEF_PAIR 15137,  6270
COEF_PAIR 15426,  5520
COEF_PAIR 15679,  4756
COEF_PAIR 16069,  3196
COEF_PAIR 16207,  2404

; ADST16-only:
COEF_PAIR  2404,  9760, 2
COEF_PAIR  5520,  7005, 2
COEF_PAIR  8423,  3981, 2
COEF_PAIR 11003,   804, 2
COEF_PAIR 12140, 16364, 5
COEF_PAIR 14053, 15893, 5
COEF_PAIR 15426, 14811, 5
COEF_PAIR 16207, 13160, 5
pw_11585_m11585:  dw 11585, -11585
pw_16069_m3196:   dw 16069,  -3196
pw_9102_m13623:   dw  9102, -13623
pw_15137_m6270:   dw 15137,  -6270
pw_6270_m15137:   dw  6270, -15137

%define pw_11585x2  pw_11585_11585x2
%define pw_m11585x2 pw_m11585_11585x2x4

SECTION .text

%define o_base pw_512 + 128
%define o(x) (r6 - (o_base) + (x))
%define m(x) mangle(private_prefix %+ _ %+ x %+ SUFFIX)

; flags: 1 = swap, 2 = interleave (l), 4 = interleave (t), 8 = no_pack,
;        16 = special_mul1, 32 = special_mul2, 64 = dst_in_tmp1
%macro ITX_MUL2X_PACK 6-7 0 ; dst/src, tmp[1-2], rnd, coef[1-2], flags
    mova                m%2, m%4
%if %7 & 16
    vpdpwssd            m%2, m%1, [o(pw_%5)] {bcstd}
    mova                m%3, m%4
%if %7 & 32
    vpdpwssd            m%3, m%1, [o(pw_%6)] {bcstd}
%else
    vpdpwssd            m%3, m%1, m%6
%endif
%elif %7 & 32
    vpdpwssd            m%2, m%1, m%5
    mova                m%3, m%4
    vpdpwssd            m%3, m%1, [o(pw_%6)] {bcstd}
%elif %6 < 32
    vpdpwssd            m%2, m%1, m%5
    mova                m%3, m%4
    vpdpwssd            m%3, m%1, m%6
%elif %7 & 1
    vpdpwssd            m%2, m%1, [o(pw_%5_%6)] {bcstd}
    mova                m%3, m%4
    vpdpwssd            m%3, m%1, [o(pw_m%6_%5)] {bcstd}
%else
    vpdpwssd            m%2, m%1, [o(pw_m%6_%5)] {bcstd}
    mova                m%3, m%4
    vpdpwssd            m%3, m%1, [o(pw_%5_%6)] {bcstd}
%endif
%if %7 & 2
    psrld               m%2, 14
    pslld               m%3, 2
    vpshrdd             m%1, m%3, m%2, 16
%elif %7 & 4
    ; compared to using shifts (as above) this has better throughput,
    ; but worse latency and requires setting up the opmask/index
    ; registers, so only use this method for the larger transforms
%if %7 & 64
    pslld               m%2, 2
    vpmultishiftqb  m%2{k7}, m13, m%3
%else
    pslld               m%1, m%2, 2
    vpmultishiftqb  m%1{k7}, m13, m%3
%endif
%else
    psrad               m%2, 14
    psrad               m%3, 14
%if %7 & 8 == 0
    packssdw            m%1, m%3, m%2
%endif
%endif
%endmacro

; dst1 = (src1 * coef1 - src2 * coef2 + rnd) >> 12
; dst2 = (src1 * coef2 + src2 * coef1 + rnd) >> 12
%macro ITX_MULSUB_2W 7 ; dst/src[1-2], tmp[1-2], rnd, coef[1-2]
    punpcklwd           m%3, m%2, m%1
    punpckhwd           m%2, m%1
%if %7 < 32
    mova                m%1, m%5
    vpdpwssd            m%1, m%3, m%7
    mova                m%4, m%5
    vpdpwssd            m%4, m%2, m%7
%else
    mova                m%1, m%5
    vpdpwssd            m%1, m%3, [o(pw_m%7_%6)] {bcstd}
    mova                m%4, m%5
    vpdpwssd            m%4, m%2, [o(pw_m%7_%6)] {bcstd}
%endif
    psrad               m%1, 14
    psrad               m%4, 14
    packssdw            m%1, m%4
    mova                m%4, m%5
%if %7 < 32
    vpdpwssd            m%4, m%2, m%6
    mova                m%2, m%5
    vpdpwssd            m%2, m%3, m%6
%else
    vpdpwssd            m%4, m%2, [o(pw_%6_%7)] {bcstd}
    mova                m%2, m%5
    vpdpwssd            m%2, m%3, [o(pw_%6_%7)] {bcstd}
%endif
    psrad               m%4, 14
    psrad               m%2, 14
    packssdw            m%2, m%4
%endmacro

; flags: 1 = swap, 2 = invert2, 4 = invert1
%macro ADST_MULSUB_4W 10-11 0 ; dst1/src1, src2, dst2, tmp[1-2], rnd, coef[1-4], flags
    mova                m%3, m%6
%if %11 & 1
    vpdpwssd            m%3, m%1, [o(pw_m%8_%7)] {bcstd}
%else
    vpdpwssd            m%3, m%1, [o(pw_%7_%8)] {bcstd}
%endif
%if %11 & 4
    vpbroadcastd        m%4, [o(pw_m%9_%10)]
%elif %11 & 2
    vpbroadcastd        m%4, [o(pw_%9_m%10)]
%elif %11 & 1
    vpbroadcastd        m%4, [o(pw_%10_%9)]
%else
    vpbroadcastd        m%4, [o(pw_%9_%10)]
%endif
    pmaddwd             m%4, m%2
    mova                m%5, m%6
%if %11 & 4
    vpdpwssd            m%5, m%1, [o(pw_%8_m%7)] {bcstd}
%elif %11 & 1
    vpdpwssd            m%5, m%1, [o(pw_%7_%8)] {bcstd}
%else
    vpdpwssd            m%5, m%1, [o(pw_m%8_%7)] {bcstd}
%endif
%if %11 & 2
    vpbroadcastd        m%1, [o(pw_%10_%9)]
%elif %11 & 1
    vpbroadcastd        m%1, [o(pw_%9_m%10)]
%else
    vpbroadcastd        m%1, [o(pw_m%10_%9)]
%endif
    pmaddwd             m%2, m%1
    paddd               m%1, m%3, m%4
    psubd               m%3, m%4
    paddd               m%4, m%5, m%2
    psubd               m%5, m%2
    pslld               m%1, 2
    pslld               m%3, 2
    vpmultishiftqb  m%1{k7}, m13, m%4
    vpmultishiftqb  m%3{k7}, m13, m%5
%endmacro

%macro WRAP_YMM 1+
    INIT_YMM cpuname
    %1
    INIT_ZMM cpuname
%endmacro

%macro INV_TXFM_FN 3-4 0 ; type1, type2, size, eob_offset
cglobal vp9_i%1_i%2_%3_add, 4, 5, 0, dst, stride, c, eob, tx2
    %undef cmp
    %define %%p1 m(vp9_i%1_%3_internal)
    lea                  r6, [o_base]
    ; Jump to the 1st txfm function if we're not taking the fast path, which
    ; in turn performs an indirect jump to the 2nd txfm function.
    lea tx2q, [m(vp9_i%2_%3_internal).pass2]
%ifidn %1_%2, dct_dct
    cmp                eobd, 1
    jne %%p1
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
    movd               xmm0, [o(pw_11585x2)]
    pmulhrsw           xmm3, xmm0, [cq]
    pxor                ym2, ym2
    pmulhrsw           xmm3, xmm0
    pmulhrsw           xmm3, [o(pw_512)]
    mova               [cq], xm2
    add                 r3d, 7
    vpbroadcastw        ym3, xmm3
.dconly_loop:
    mova                xm1, [dstq+strideq*0]
    vinserti32x4        ym1, [dstq+strideq*1], 1
    punpcklbw           ym0, ym1, ym2
    punpckhbw           ym1, ym2
    paddw               ym0, ym3
    paddw               ym1, ym3
    packuswb            ym0, ym1
    mova          [dstq+strideq*0], xm0
    vextracti32x4 [dstq+strideq*1], ym0, 1
    lea                dstq, [dstq+strideq*2]
    dec                 r3d
    jg .dconly_loop
    RET
%endif
%endmacro

%macro IDCT16_MAIN 0-1 0 ; idct32
%if mmsize == 64 && %1 == 0
.main_fast:
%endif
    vpbroadcastd         m2, [o(pw_1606_16305x2)]
    vpbroadcastd         m4, [o(pw_m10394_12665x2)]
    vpbroadcastd        m11, [o(pw_7723_14449x2)]
    vpbroadcastd        m12, [o(pw_m4756_15679x2)]
    pmulhrsw             m8, m2  ; t8a  t15a
    vpbroadcastd         m2, [o(pw_3196_16069x2)]
    pmulhrsw             m0, m4  ; t9a  t14a
    vpbroadcastd         m4, [o(pw_m9102_13623x2)]
    pmulhrsw             m5, m11 ; t10a t13a
    vpbroadcastd        m11, [o(pw_11585_11585x2)]
    pmulhrsw             m1, m12 ; t11a t12a
    vbroadcasti32x4     m12, [o(pw_15137_6270x2x4)]
    pmulhrsw             m7, m2  ; t4a  t7a
    pmulhrsw             m3, m4  ; t5a  t6a
    pmulhrsw             m9, m11 ; t0   t1
    pmulhrsw             m6, m12 ; t3   t2
%if mmsize == 64 && %1 == 0
    jmp %%main2
ALIGN function_align
.main:
    punpckhwd            m8, m7, m0 ; dct16 in15 in1
    punpcklwd            m9, m4, m0 ; dct4  in2  in0
    punpckhwd            m0, m3, m4 ; dct16 in7  in9
    punpcklwd            m7, m1     ; dct8  in7  in1
    punpckhwd            m1, m6     ; dct16 in3  in13
    punpcklwd            m3, m5     ; dct8  in3  in5
    punpckhwd            m5, m2     ; dct16 in11 in5
    punpcklwd            m6, m2     ; dct4  in3  in1
    ITX_MUL2X_PACK        8, 2, 4, 10,  1606, 16305, 5 ; t8a  t15a
    ITX_MUL2X_PACK        0, 2, 4, 10, 12665, 10394, 5 ; t9a  t14a
    ITX_MUL2X_PACK        5, 2, 4, 10,  7723, 14449, 5 ; t10a t13a
    ITX_MUL2X_PACK        1, 2, 4, 10, 15679,  4756, 5 ; t11a t12a
    ITX_MUL2X_PACK        7, 2, 4, 10,  3196, 16069, 5 ; t4a  t7a
    ITX_MUL2X_PACK        3, 2, 4, 10, 13623,  9102, 5 ; t5a  t6a
    ITX_MUL2X_PACK        9, 2, 4, 10, 11585, 11585    ; t0   t1
    ITX_MUL2X_PACK        6, 2, 4, 10,  6270, 15137    ; t3   t2
%%main2:
%endif
    psubw                m2, m8, m0 ; t9  t14
    paddw                m8, m0     ; t8  t15
    psubw                m4, m1, m5 ; t10 t13
    paddw                m1, m5     ; t11 t12
    ITX_MUL2X_PACK        2, 0, 5, 10,   6270, 15137, (1|%1*4) ; t9a  t14a
    ITX_MUL2X_PACK        4, 0, 5, 10, m15137,  6270, (1|%1*4) ; t10a t13a
    vbroadcasti32x4      m5, [o(deint_shuf)]
    psubw                m0, m8, m1 ; t11a t12a
    paddw                m8, m1     ; t8a  t15a
    psubw                m1, m7, m3 ; t5a  t6a
    paddw                m7, m3     ; t4   t7
    pshufb               m8, m5
    pshufb               m7, m5
    paddw                m3, m2, m4 ; t9   t14
    psubw                m2, m4     ; t10  t13
%if %1
    vpbroadcastd        m12, [o(pw_11585_11585)]
    vpbroadcastd        m11, [o(pw_m11585_11585)]
    pshufb               m3, m5
    ITX_MUL2X_PACK        1, 4,  5, 10, 12, 11    ; t5   t6
    ITX_MUL2X_PACK        0, 4,  5, 10, 11, 12, 8 ; t11  t12
    ITX_MUL2X_PACK        2, 0, 11, 10, 11, 12, 8 ; t10a t13a
    packssdw             m5, m11    ; t12  t13a
    packssdw             m4, m0     ; t11  t10a
%else
    pshufb               m0, m5
    ITX_MUL2X_PACK        1, 4, 5, 10, 11585_11585, m11585_11585, 48 ; t5   t6
    vpbroadcastd        m11, [o(pw_11585x2)]
    punpckhqdq           m5, m0, m2 ; t12a t13
    punpcklqdq           m0, m2     ; t11a t10
    psubw                m4, m5, m0
    paddw                m5, m0
    pmulhrsw             m4, m11    ; t11  t10a
    pmulhrsw             m5, m11    ; t12  t13a
%endif
    punpckhqdq           m2, m7, m1 ; t7   t6
    punpcklqdq           m7, m1     ; t4   t5
    psubw                m1, m9, m6 ; t3   t2
    paddw                m9, m6     ; t0   t1
    punpckhqdq           m0, m8, m3 ; t15a t14
    punpcklqdq           m8, m3     ; t8a  t9
    psubw                m3, m9, m2 ; t7   t6
    paddw                m9, m2     ; t0   t1
    psubw                m2, m1, m7 ; t4   t5
    paddw                m1, m7     ; t3   t2
    psubw                m7, m9, m0 ; out15 out14
    paddw                m0, m9     ; out0  out1
    psubw                m6, m1, m5 ; out12 out13
    paddw                m1, m5     ; out3  out2
    psubw                m5, m2, m4 ; out11 out10
    paddw                m2, m4     ; out4  out5
    psubw                m4, m3, m8 ; out8  out9
    paddw                m3, m8     ; out7  out6
%endmacro

INIT_ZMM avx512icl
INV_TXFM_16X16_FN dct, dct
INV_TXFM_16X16_FN dct, adst, 39-23

cglobal vp9_idct_16x16_internal, 0, 5, 16, dst, stride, c, eob, tx2
    mova                m15, [o(itx_perm)]
    vpbroadcastd        m10, [o(pd_8192)]
    vpbroadcastq        m13, [o(int_mshift)]
    vpcmpub              k7, m13, m10, 6
    sub                eobd, 39
    jl .pass1_fast
    vpermq               m0, m15, [cq+64*0]
    vpermq               m1, m15, [cq+64*1]
    vpermq               m2, m15, [cq+64*2]
    vpermq               m3, m15, [cq+64*3]
    vpermq               m4, m15, [cq+64*4]
    vpermq               m5, m15, [cq+64*5]
    vpermq               m6, m15, [cq+64*6]
    vpermq               m7, m15, [cq+64*7]
    call .main
    vbroadcasti32x4     m12, [o(int_shuf1)]
    vbroadcasti32x4     m11, [o(int_shuf2)]
    pshufb               m0, m12
    pshufb               m8, m1, m11
    pshufb               m2, m12
    pshufb               m9, m3, m11
    pshufb               m4, m12
    pshufb              m14, m5, m11
    pshufb               m6, m12
    pshufb              m11, m7, m11
    punpckhdq            m1, m0, m8
    punpckldq            m0, m8
    punpckhdq            m3, m2, m9
    punpckldq            m2, m9
    punpckhdq            m5, m4, m14
    punpckldq            m4, m14
    punpckhdq            m7, m6, m11
    punpckldq            m6, m11
.pass1_end:
    vshufi32x4           m8, m4, m6, q3232
    vinserti32x8         m4, ym6, 1
    vshufi32x4           m6, m0, m2, q3232
    vinserti32x8         m0, ym2, 1
    vshufi32x4           m9, m5, m7, q3232
    vinserti32x8         m5, ym7, 1
    vshufi32x4           m7, m1, m3, q3232
    vinserti32x8         m1, ym3, 1
    vshufi32x4           m2, m0, m4, q3131 ;  4  5
    vshufi32x4           m0, m4, q2020     ;  0  1
    vshufi32x4           m4, m6, m8, q2020 ;  8  9
    vshufi32x4           m6, m8, q3131     ; 12 13
    vshufi32x4           m3, m1, m5, q3131 ;  6  7
    vshufi32x4           m1, m5, q2020     ;  2  3
    vshufi32x4           m5, m7, m9, q2020 ; 10 11
    vshufi32x4           m7, m9, q3131     ; 14  1
    jmp                tx2q
.pass1_fast:
    mova                ym3, [o(dup16_perm)]
    vbroadcasti32x4     ym9, [cq+32*0]
    vbroadcasti32x4     ym6, [cq+32*4]
    vpermb              ym8, ym3, [cq+32*1]
    vpermb              ym0, ym3, [cq+32*7]
    vpermb              ym5, ym3, [cq+32*5]
    vpermb              ym1, ym3, [cq+32*3]
    vpermb              ym7, ym3, [cq+32*2]
    vpermb              ym3, ym3, [cq+32*6]
    shufpd              ym9, ym9, 0x0c
    shufpd              ym6, ym6, 0x0c
    WRAP_YMM IDCT16_MAIN
    vbroadcasti32x4      m8, [o(int_shuf1)]
    vbroadcasti32x4      m9, [o(int_shuf2)]
    vinserti32x8         m0, ym2, 1 ;  0  1 |  4  5
    vinserti32x8         m4, ym6, 1 ;  8  9 | 12 13
    vinserti32x8         m1, ym3, 1 ;  3  2 |  7  6
    vinserti32x8         m5, ym7, 1 ; 11 10 | 15 14
    vshufi32x4           m2, m0, m4, q3131
    vshufi32x4           m0, m4, q2020
    vshufi32x4           m4, m1, m5, q2020
    vshufi32x4           m1, m5, q3131
    pshufb               m2, m8
    pshufb               m0, m8
    pshufb               m4, m9
    pshufb               m1, m9
    punpckhdq            m3, m2, m1 ; 6-7
    punpckldq            m2, m1     ; 4-5
    punpckhdq            m1, m0, m4 ; 2-3
    punpckldq            m0, m4     ; 0-1
    jmp                tx2q
.pass2:
    test               eobd, eobd
    jl .pass2_fast
    call .main
    jmp .pass2_end
.pass2_fast:
    punpcklqdq           m9, m0, m0
    punpckhwd            m8, m0, m0
    punpcklwd            m7, m1, m1
    punpckhwd            m1, m1
    punpcklqdq           m6, m2, m2
    punpckhwd            m5, m2, m2
    punpckhwd            m0, m3, m3
    punpcklwd            m3, m3
    call .main_fast
.pass2_end:
    psrldq               m8, m15, 1
    psrlq               m12, m15, 12
    psrldq               m9, m15, 2
    psrlq               m13, m15, 20
    mova                m10, m8
    vpermi2q             m8, m0, m2 ;  0  1  4  5
    vpermt2q             m0, m12, m2
    mova                m11, m9
    vpermi2q             m9, m1, m3 ;  2  3  6  7
    vpermt2q             m1, m13, m3
    vpbroadcastd         m2, [o(pw_512)]
    vpermi2q            m10, m4, m6 ;  8  9 12 13
    vpermt2q             m4, m12, m6
    vpermi2q            m11, m5, m7 ; 10 11 14 15
    vpermt2q             m5, m13, m7
    REPX   {pmulhrsw x, m2}, m0, m1, m4, m5, m8, m9, m10, m11
.pass2_end2:
    lea                  r3, [strideq*3]
    lea                  r4, [dstq+strideq*4]
    lea                  r5, [dstq+strideq*8]
    lea                  r6, [r4  +strideq*8]
    mova                xm3, [dstq+strideq*0]
    mova                xm6, [dstq+strideq*2]
    vinserti32x4        ym3, [dstq+strideq*1], 1
    vinserti32x4        ym6, [dstq+r3       ], 1
    vinserti32x4         m3, [r4+strideq*0], 2
    vinserti32x4         m6, [r4+strideq*2], 2
    vinserti32x4         m3, [r4+strideq*1], 3
    vinserti32x4         m6, [r4+r3       ], 3
    mova               xm12, [r5+strideq*0]
    mova               xm13, [r5+strideq*2]
    vinserti32x4       ym12, [r5+strideq*1], 1
    vinserti32x4       ym13, [r5+r3       ], 1
    vinserti32x4        m12, [r6+strideq*0], 2
    vinserti32x4        m13, [r6+strideq*2], 2
    vinserti32x4        m12, [r6+strideq*1], 3
    vinserti32x4        m13, [r6+r3       ], 3
    pxor                 m7, m7
    REPX {mova [cq+64*x], m7}, 0, 1, 2, 3, 4, 5, 6, 7
    punpcklbw            m2, m3, m7
    punpckhbw            m3, m7
    paddw                m0, m2
    paddw                m8, m3
    packuswb             m0, m8
    punpcklbw            m2, m6, m7
    punpckhbw            m6, m7
    paddw                m1, m2
    paddw                m9, m6
    packuswb             m1, m9
    punpcklbw            m2, m12, m7
    punpckhbw           m12, m7
    paddw                m2, m4
    paddw               m10, m12
    packuswb             m2, m10
    punpcklbw            m3, m13, m7
    punpckhbw           m13, m7
    paddw                m3, m5
    paddw               m11, m13
    packuswb             m3, m11
    mova          [dstq+strideq*0], xm0
    vextracti32x4 [dstq+strideq*1], ym0, 1
    mova          [dstq+strideq*2], xm1
    vextracti32x4 [dstq+r3       ], ym1, 1
    vextracti32x4 [r4+strideq*0], m0, 2
    vextracti32x4 [r4+strideq*1], m0, 3
    vextracti32x4 [r4+strideq*2], m1, 2
    vextracti32x4 [r4+r3       ], m1, 3
    mova          [r5+strideq*0], xm2
    vextracti32x4 [r5+strideq*1], ym2, 1
    mova          [r5+strideq*2], xm3
    vextracti32x4 [r5+r3       ], ym3, 1
    vextracti32x4 [r6+strideq*0], m2, 2
    vextracti32x4 [r6+strideq*1], m2, 3
    vextracti32x4 [r6+strideq*2], m3, 2
    vextracti32x4 [r6+r3       ], m3, 3
    RET
ALIGN function_align
    IDCT16_MAIN
    ret

%macro IADST16_MAIN 0
%if mmsize == 64
.main_fast:
%endif
    punpcklwd            m4, m3, m0 ; in7 in0
    punpcklwd           m11, m1, m2 ; in3 in4
    punpckhwd            m9, m2, m1 ; in5 in2
    punpckhwd            m7, m0, m3 ; in1 in6
    ITX_MUL2X_PACK        4, 0, 6, 10,  11003_804,  12140_m16364, 116 ; t1a  t0a
    ITX_MUL2X_PACK        4, 5, 6, 10, m11003_804, m12140_m16364,  52 ; t9a  t8a
    ITX_MUL2X_PACK       11, 2, 6, 10,  5520_7005,  15426_m14811, 116 ; t5a  t4a
    ITX_MUL2X_PACK       11, 5, 6, 10, m5520_7005, m15426_m14811,  52 ; t13a t12a
    ITX_MUL2X_PACK        9, 1, 6, 10,  8423_3981,  14053_m15893, 116 ; t3a  t2a
    ITX_MUL2X_PACK        9, 5, 6, 10, m8423_3981, m14053_m15893,  52 ; t11a t10a
    ITX_MUL2X_PACK        7, 3, 6, 10,  2404_9760,  16207_m13160, 116 ; t7a  t6a
    ITX_MUL2X_PACK        7, 5, 6, 10, m2404_9760, m16207_m13160,  52 ; t15a t14a
%if mmsize == 64 ; for the ymm variant we only ever use the fast path
    jmp %%main2
ALIGN function_align
.main:
    punpckhwd            m8, m7, m0 ; in14 in1
    punpcklwd            m0, m7     ; in0  in15
    punpcklwd            m7, m6, m1 ; in12 in3
    punpckhwd            m1, m6     ; in2  in13
    punpckhwd            m6, m5, m2 ; in10 in5
    punpcklwd            m2, m5     ; in4  in11
    punpcklwd            m5, m4, m3 ; in8  in7
    punpckhwd            m3, m4     ; in6  in9
    ADST_MULSUB_4W        0,  5,  4,  9, 11, 10,   804, 16364, 12140, 11003    ;  t1a    t0a,  t9a    t8a
    ADST_MULSUB_4W        2,  7, 11,  5,  9, 10,  7005, 14811, 15426,  5520    ;  t5a    t4a,  t13a   t12a
    ADST_MULSUB_4W        1,  6,  9,  5,  7, 10,  3981, 15893, 14053,  8423    ;  t3a    t2a,  t11a   t10a
    ADST_MULSUB_4W        3,  8,  7,  5,  6, 10,  9760, 13160, 16207,  2404    ;  t7a    t6a,  t15a   t14a
%%main2:
%endif
    psubw                m5, m1, m3        ;  t7     t6
    paddw                m6, m1, m3        ;  t3     t2
    psubw                m1, m0, m2        ;  t5     t4
    paddw                m2, m0            ;  t1     t0
    ADST_MULSUB_4W        4, 11,  8,  3,  0, 10,  3196, 16069, 16069,  3196, 1 ;  t8a    t9a,  t12a   t13a
    ADST_MULSUB_4W        9,  7,  0,  3, 11, 10, 13623,  9102,  9102, 13623, 1 ;  t10a   t11a, t14a   t15a
    ADST_MULSUB_4W        1,  5, 11,  3,  7, 10,  6270, 15137, 15137,  6270, 2 ;  out12 -out3, t7     t6
    psubw                m3, m2, m6        ;  t3a    t2a
    paddw                m2, m6            ; -out15  out0
    ADST_MULSUB_4W        8,  0,  5,  6,  7, 10, 15137,  6270,  6270, 15137, 6 ; -out13  out2, t15a   t14
    vbroadcasti32x4     m12, [o(deint_shuf)]
    paddw                m0, m4, m9        ; -out1   out14
    psubw                m4, m9            ;  t10    t11
    pshufb               m2, m12
    pshufb               m1, m12
    pshufb               m8, m12
    pshufb               m0, m12
    punpcklqdq           m6, m1, m8        ;  out12 -out13
    shufps               m7, m0, m2, q1032 ;  out14 -out15
%endmacro

%macro IADST16_PASS1_END 0
    shufps               m0, m2, m0, q1032 ;  out0  -out1
    punpckhqdq           m1, m8, m1        ;  out2  -out3
    mova                 m2, m10
    vpdpwssd             m2, m5, [o(pw_m11585_m11585)] {bcstd} ; out5
    mova                 m8, m10
    vpdpwssd             m8, m11, [o(pw_11585_11585)]  {bcstd} ; out4
    mova                 m9, m10
    vpdpwssd             m9, m5, [o(pw_m11585_11585)]  {bcstd} ; out10
    mova                 m5, m10
    vpdpwssd             m5, m11, [o(pw_11585_m11585)] {bcstd} ; out11
    mova                m11, m10
    vpdpwssd            m11, m3, [o(pw_m11585_m11585)] {bcstd} ; out7
    mova                m14, m10
    vpdpwssd            m14, m4, [o(pw_11585_11585)]   {bcstd} ; out6
    mova                m12, m10
    vpdpwssd            m12, m3, [o(pw_m11585_11585)]  {bcstd} ; out8
    mova                 m3, m10
    vpdpwssd             m3, m4, [o(pw_m11585_11585)]  {bcstd} ; out9
%endmacro

INV_TXFM_16X16_FN adst, dct, 39-18
INV_TXFM_16X16_FN adst, adst

cglobal vp9_iadst_16x16_internal, 0, 5, 16, dst, stride, c, eob, tx2
    mova                m15, [o(itx_perm)]
    psrlq                m7, m15, 4
    vpermq               m0, m15, [cq+64*0] ;  0  1
    vpermq               m1, m7, [cq+64*1]  ;  3  2
    vpermq               m2, m15, [cq+64*2] ;  4  5
    vpermq               m3, m7, [cq+64*3]  ;  7  6
    vpbroadcastd        m10, [o(pd_8192)]
    vpbroadcastq        m13, [o(int_mshift)]
    vpcmpub              k7, m13, m10, 6
    sub                eobd, 39
    jl .pass1_fast
    vpermq               m4, m15, [cq+64*4] ;  8  9
    vpermq               m5, m7, [cq+64*5]  ; 11 10
    vpermq               m6, m15, [cq+64*6] ; 12 13
    vpermq               m7, m7, [cq+64*7]  ; 15 14
    call .main
    IADST16_PASS1_END
    REPX      {psrad x, 14}, m2, m8, m9, m5, m11, m14, m12, m3
    packssdw             m2, m8, m2   ; out4  out5
    packssdw             m5, m9, m5   ; out10 out11
    packssdw             m4, m12, m3  ; out8  out9
    packssdw             m3, m14, m11 ; out6  out7
    pxor                 m9, m9
    punpckhwd            m8, m0, m1
    punpcklwd            m0, m1
    psubw                m8, m9, m8
    punpckhwd            m1, m0, m8
    punpcklwd            m0, m8
    punpckhwd            m8, m2, m3
    punpcklwd            m2, m3
    punpckhwd            m3, m2, m8
    punpcklwd            m2, m8
    punpckhwd            m8, m4, m5
    punpcklwd            m4, m5
    punpckhwd            m5, m4, m8
    punpcklwd            m4, m8
    punpckhwd            m8, m6, m7
    punpcklwd            m6, m7
    psubw                m8, m9, m8
    punpckhwd            m7, m6, m8
    punpcklwd            m6, m8
    jmp m(vp9_idct_16x16_internal).pass1_end
.pass1_fast:
    WRAP_YMM IADST16_MAIN
    WRAP_YMM IADST16_PASS1_END
    vinserti32x8         m0, ym6, 1
    vinserti32x8         m1, ym7, 1
    vinserti32x8         m8, ym12, 1
    vinserti32x8         m2, ym3, 1
    vinserti32x8        m14, ym9, 1
    vinserti32x8        m11, ym5, 1
    pslld               m14, 2
    pslld               m11, 2
    punpckhwd            m4, m0, m1
    punpcklwd            m0, m1
    vpmultishiftqb  m14{k7}, m13, m8
    vpmultishiftqb  m11{k7}, m13, m2
    psrlq                m1, m15, 24
    pxor                 m2, m2
    psubw                m2, m4
    punpckhwd            m3, m0, m2
    punpcklwd            m0, m2
    psrlq                m2, m15, 28
    punpckhwd            m4, m14, m11
    punpcklwd           m14, m11
    mova                 m5, m2
    vpermi2q             m2, m0, m14
    vpermt2q             m0, m1, m14
    vpermi2q             m1, m3, m4
    vpermt2q             m3, m5, m4
    jmp                tx2q
.pass2:
    pshufd               m1, m1, q1032
    pshufd               m3, m3, q1032
    test               eobd, eobd
    jl .pass2_fast
    pshufd               m5, m5, q1032
    pshufd               m7, m7, q1032
    call .main
    jmp .pass2_end
.pass2_fast:
    call .main_fast
.pass2_end:
    vbroadcasti32x4      m9, [o(pw_11585_m11585x2x4)]
    vbroadcasti32x4     m10, [o(pw_m11585_11585x2x4)]
    punpckhqdq           m1, m8            ; -out3   out2
    shufps               m0, m2, q3210     ; -out1   out0
    pshufb               m2, m11, m12
    pshufb               m5, m12
    pshufb               m3, m12
    pshufb               m4, m12
    vbroadcasti32x4     m11, [o(pw_512)]
    vpbroadcastd        m12, [o(pw_512)]
    punpcklqdq           m8, m5, m2        ; t15a  t7
    punpckhqdq           m5, m2            ; t14a  t6
    shufps               m2, m3, m4, q1032 ; t2a   t10
    shufps               m3, m4, q3210     ; t3a   t11
    psubsw               m4, m2, m3
    paddsw               m3, m2
    paddsw               m2, m5, m8
    psubsw               m5, m8
    pmulhrsw             m4, m9            ; out8  out9
    pmulhrsw             m3, m10           ; out7  out6
    pmulhrsw             m2, m10           ; out5  out4
    pmulhrsw             m5, m9            ; out10 out11
    pmulhrsw             m6, m11
    pmulhrsw             m7, m11
    pshufd              m11, m11, q1032
    pmulhrsw             m0, m11
    pmulhrsw             m1, m11
    REPX  {pmulhrsw x, m12}, m2, m3, m4, m5
    psrldq               m8, m15, 2
    psrlq               m12, m15, 20
    psrldq              m10, m15, 1
    psrlq               m13, m15, 12
    mova                 m9, m8
    vpermi2q             m8, m0, m2  ;  0  1  4  5
    vpermt2q             m0, m12, m2
    vpermi2q             m9, m1, m3  ;  2  3  6  7
    vpermt2q             m1, m12, m3
    mova                m11, m10
    vpermi2q            m10, m4, m6  ;  8  9 12 13
    vpermt2q             m4, m13, m6
    vpermi2q            m11, m5, m7  ; 10 11 14 15
    vpermt2q             m5, m13, m7
    jmp m(vp9_idct_16x16_internal).pass2_end2
ALIGN function_align
    IADST16_MAIN
    ret

%macro IDCT_32x32_END 4 ; src, mem, stride[1-2]
    pmovzxbw            m10, [dstq+%3]
    pmovzxbw            m11, [r3  +%4]
%if %2 < 8
    paddw                m8, m%2, m%1
    psubw                m9, m%2, m%1
%else
    mova                 m9, [rsp+64*(%2-8)]
    paddw                m8, m9, m%1
    psubw                m9, m%1
%endif
    pmulhrsw             m8, m12
    pmulhrsw             m9, m12
    paddw                m8, m10
    paddw                m9, m11
    packuswb             m8, m9
    vpermq               m8, m13, m8
    mova          [dstq+%3], ym8
    vextracti32x8 [r3  +%4], m8, 1
%if %2 == 3 || %2 == 7 || %2 == 11
    add                dstq, r5
    sub                  r3, r5
%endif
%endmacro

cglobal vp9_idct_idct_32x32_add, 4, 7, 0, dst, stride, c, eob
%undef cmp
    lea                  r6, [o_base]
    cmp                eobd, 1
    jne .pass1
    movd               xmm0, [o(pw_11585x2)]
    pmulhrsw           xmm3, xmm0, [cq]
    pxor                 m2, m2
    pmulhrsw           xmm3, xmm0
    pmulhrsw           xmm3, [o(pw_512)]
    movd               [cq], xm2
    add                 r3d, 15
    vpbroadcastw         m3, xmm3
.dconly_loop:
    mova                ym1, [dstq+strideq*0]
    vinserti32x8         m1, [dstq+strideq*1], 1
    punpcklbw            m0, m1, m2
    punpckhbw            m1, m2
    paddw                m0, m3
    paddw                m1, m3
    packuswb             m0, m1
    mova          [dstq+strideq*0], ym0
    vextracti32x8 [dstq+strideq*1], m0, 1
    lea                dstq, [dstq+strideq*2]
    dec                 r3d
    jg .dconly_loop
    RET
.pass1:
    PROLOGUE 0, 7, 30, 64*16, dst, stride, c, eob
    sub                eobd, 135
    jl .fast
    mova                 m0, [cq+64* 0]
    mova                m14, [cq+64* 2]
    mova                 m1, [cq+64* 4]
    mova                m15, [cq+64* 6]
    mova                 m2, [cq+64* 8]
    mova                m16, [cq+64*10]
    mova                 m3, [cq+64*12]
    mova                m17, [cq+64*14]
    mova                 m4, [cq+64*16]
    mova                m18, [cq+64*18]
    mova                 m5, [cq+64*20]
    mova                m19, [cq+64*22]
    mova                 m6, [cq+64*24]
    mova                m20, [cq+64*26]
    mova                 m7, [cq+64*28]
    mova                m21, [cq+64*30]
    call .idct16
    mova         [rsp+64*0], m14
    mova         [rsp+64*1], m15
    mova         [rsp+64*2], m16
    mova         [rsp+64*3], m17
    mova         [rsp+64*4], m18
    mova         [rsp+64*5], m19
    mova         [rsp+64*6], m20
    mova         [rsp+64*7], m21
    mova                m22, [cq+64* 1]
    mova                m23, [cq+64* 3]
    mova                m24, [cq+64* 5]
    mova                m25, [cq+64* 7]
    mova                m26, [cq+64* 9]
    mova                m27, [cq+64*11]
    mova                m28, [cq+64*13]
    mova                m29, [cq+64*15]
    mova                m14, [cq+64*17]
    mova                m15, [cq+64*19]
    mova                m16, [cq+64*21]
    mova                m17, [cq+64*23]
    mova                m18, [cq+64*25]
    mova                m19, [cq+64*27]
    mova                m20, [cq+64*29]
    mova                m21, [cq+64*31]
    call .main
    psubw               m13, m0, m29 ; 31
    paddw                m0, m29     ;  0
    psubw               m29, m1, m28 ; 30
    paddw                m1, m28     ;  1
    psubw               m28, m2, m27 ; 29
    paddw                m2, m27     ;  2
    psubw               m27, m3, m26 ; 28
    paddw                m3, m26     ;  3
    psubw               m26, m4, m25 ; 27
    paddw                m4, m25     ;  4
    psubw               m25, m5, m24 ; 26
    paddw                m5, m24     ;  5
    psubw               m24, m6, m23 ; 25
    paddw                m6, m23     ;  6
    psubw               m23, m7, m22 ; 24
    paddw                m7, m22     ;  7
    punpckhwd            m8, m0, m1  ; a4 b4 a5 b5 a6 b6 a7 b7
    punpcklwd            m0, m1      ; a0 b0 a1 b1 a2 b2 a3 b3
    punpckhwd            m1, m2, m3  ; c4 d4 c5 d5 c6 d6 c7 d7
    punpcklwd            m2, m3      ; c0 d0 c1 d1 c2 d2 c3 d3
    punpckhwd           m22, m4, m5  ; e4 f4 e5 f5 e6 f6 e7 f7
    punpcklwd            m4, m5      ; e0 f0 e1 f1 e2 f2 e3 f3
    punpckhwd            m5, m6, m7  ; g4 h4 g5 h5 g6 h6 g7 h7
    punpcklwd            m6, m7      ; g0 h0 g1 h1 g2 h2 g3 h3
    punpckhwd            m3, m23, m24
    punpcklwd           m23, m24
    punpckhwd           m24, m25, m26
    punpcklwd           m25, m26
    punpckhwd           m26, m27, m28
    punpcklwd           m27, m28
    punpckhwd           m28, m29, m13
    punpcklwd           m29, m13
    punpckhdq            m7, m0, m2  ; a2 b2 c2 d2 a3 b3 c3 d3
    punpckldq            m0, m2      ; a0 b0 c0 d0 a1 b1 c1 d1
    punpckhdq            m2, m4, m6  ; e2 f2 g2 h2 e3 f3 g3 h3
    punpckldq            m4, m6      ; e0 f0 g0 h0 e1 f1 g1 h1
    punpckhdq            m6, m8, m1  ; a6 b6 c6 d6 a7 b7 c7 d7
    punpckldq            m8, m1      ; a4 b4 c4 d4 a5 b5 c5 d5
    punpckhdq            m1, m22, m5 ; e6 f6 g6 h6 e7 f7 g7 h7
    punpckldq           m22, m5      ; e4 f4 g4 h5 e5 f5 g5 h5
    punpckhdq           m13, m23, m25
    punpckldq           m23, m25
    punpckhdq           m25, m27, m29
    punpckldq           m27, m29
    punpckhdq            m9, m3, m24
    punpckldq            m3, m24
    punpckhdq           m24, m26, m28
    punpckldq           m26, m28
    punpcklqdq           m5, m23, m27 ; d00 d08 d16 d24
    punpckhqdq          m23, m27      ; d01 d09 d17 d25
    punpckhqdq          m27, m13, m25 ; d03 d11 d19 d27
    punpcklqdq          m13, m25      ; d02 d10 d18 d26
    punpckhqdq          m25, m3, m26  ; d05 d13 d21 d29
    punpcklqdq           m3, m26      ; d04 d12 d20 d28
    punpckhqdq          m26, m9, m24  ; d07 d15 d23 d31
    punpcklqdq           m9, m24      ; d06 d14 d22 d30
    mova        [rsp+64*12], m23
    mova        [rsp+64*13], m27
    mova        [rsp+64*14], m25
    mova        [rsp+64*15], m26
    punpckhqdq          m24, m8, m22  ; a05 a13 a21 a29
    punpcklqdq           m8, m22      ; a04 a12 a20 a28
    punpckhqdq          m22, m0, m4   ; a01 a09 a17 a25
    punpcklqdq           m0, m4       ; a00 a08 a16 a24
    punpckhqdq          m23, m7, m2   ; a03 a11 a19 a27
    punpcklqdq           m7, m2       ; a02 a10 a18 a26
    punpckhqdq          m25, m6, m1   ; a07 a15 a23 a31
    punpcklqdq           m6, m1       ; a06 a14 a22 a30
    mova                 m2, [rsp+64*0]
    mova                m11, [rsp+64*1]
    mova                m12, [rsp+64*2]
    mova                m29, [rsp+64*3]
    mova                m27, [rsp+64*4]
    mova                m26, [rsp+64*5]
    mova                 m4, [rsp+64*6]
    mova                m28, [rsp+64*7]
    psubw                m1, m2, m21  ; 23
    paddw                m2, m21      ;  8
    psubw               m21, m11, m20 ; 22
    paddw               m11, m20      ;  9
    psubw               m20, m12, m19 ; 21
    paddw               m12, m19      ; 10
    psubw               m19, m29, m18 ; 20
    paddw               m29, m18      ; 11
    psubw               m18, m27, m17 ; 19
    paddw               m27, m17      ; 12
    psubw               m17, m26, m16 ; 18
    paddw               m26, m16      ; 13
    paddw               m16, m4, m15  ; 14
    psubw                m4, m15      ; 17
    mova                m15, m6
    psubw                m6, m28, m14 ; 16
    paddw               m28, m14      ; 15
    mova                m14, m7
    punpcklwd            m7, m6, m4
    punpckhwd            m6, m4
    punpckhwd            m4, m17, m18
    punpcklwd           m17, m18
    punpckhwd           m18, m19, m20
    punpcklwd           m19, m20
    punpckhwd           m20, m21, m1
    punpcklwd           m21, m1
    punpckhwd            m1, m2, m11  ; i4 j4 i5 j5 i6 j6 i7 j7
    punpcklwd            m2, m11      ; i0 j1 i1 j1 i2 j2 i3 j3
    punpckhwd           m11, m12, m29 ; k4 l4 k5 l5 k6 l6 k7 l7
    punpcklwd           m12, m29      ; k0 l0 k1 l1 k2 l2 k3 l3
    punpckhwd           m29, m27, m26 ; m4 n4 m5 n5 m6 n6 m7 n7
    punpcklwd           m27, m26      ; m0 n0 m1 n1 m2 n2 m3 n3
    punpckhwd           m26, m16, m28 ; o4 p4 o5 p5 o6 p6 o7 p7
    punpcklwd           m16, m28      ; o0 p0 o1 p1 o2 p2 o3 p3
    punpckhdq           m28, m2, m12  ; i2 j2 k2 l2 i3 j3 k3 l3
    punpckldq            m2, m12      ; i0 j0 k0 l0 i1 j1 k1 l1
    punpckhdq           m12, m27, m16 ; m2 n2 o2 p2 m3 n3 o3 p3
    punpckldq           m27, m16      ; m0 n0 o0 p0 m1 n1 o1 p1
    punpckhdq           m16, m1, m11  ; i6 j6 k6 l6 i7 j7 k7 l7
    punpckldq            m1, m11      ; i4 j4 k4 l4 i5 j5 k5 l5
    punpckhdq           m11, m29, m26 ; m6 n6 o6 p6 m7 n7 o7 p7
    punpckldq           m29, m26      ; m4 n4 o4 p4 m5 n5 o5 p5
    punpckhdq           m26, m19, m21
    punpckldq           m19, m21
    punpckhdq           m21, m6, m4
    punpckldq            m6, m4
    punpckhdq            m4, m18, m20
    punpckldq           m18, m20
    punpckhdq           m20, m7, m17
    punpckldq            m7, m17
    punpcklqdq          m17, m28, m12 ; b02 b10 b18 b26
    punpckhqdq          m28, m12      ; b03 b11 b19 b27
    punpckhqdq          m12, m2, m27  ; b01 b09 b17 b25
    punpcklqdq           m2, m27      ; b00 b08 b16 b24
    punpckhqdq          m27, m1, m29  ; b05 b13 b21 b29
    punpcklqdq           m1, m29      ; b04 b12 b20 b28
    punpckhqdq          m29, m16, m11 ; b07 b15 b23 b31
    punpcklqdq          m16, m11      ; b06 b14 b22 b30
    mova        [rsp+64* 8], m12
    mova        [rsp+64* 9], m28
    mova        [rsp+64*10], m27
    mova        [rsp+64*11], m29
    punpckhqdq          m27, m20, m26 ; c03 c11 c19 c27
    punpcklqdq          m20, m26      ; c02 c10 c18 c26
    punpckhqdq          m26, m7, m19  ; c01 c09 c17 c25
    punpcklqdq           m7, m19      ; c00 c08 c16 c24
    punpckhqdq          m28, m6, m18  ; c05 c13 c21 c29
    punpcklqdq           m6, m18      ; c04 c12 c20 c28
    punpckhqdq          m29, m21, m4  ; c07 c15 c23 c31
    punpcklqdq          m21, m4       ; c06 c14 c22 c30
    mov                 r3d, 64*28
    pxor                 m4, m4
.zero_loop:
    mova       [cq+r3+64*0], m4
    mova       [cq+r3+64*1], m4
    mova       [cq+r3+64*2], m4
    mova       [cq+r3+64*3], m4
    sub                 r3d, 64*4
    jge .zero_loop
    vshufi32x4           m4, m0, m2, q3232   ; a16 a24 b16 b24
    vinserti32x8         m0, ym2, 1          ; a00 a08 b00 b08
    vshufi32x4           m2, m7, m5, q3232   ; c16 c24 d16 d24
    vinserti32x8         m7, ym5, 1          ; c00 c08 d00 d08
    vshufi32x4           m5, m8, m1, q3232   ; a20 a28 b20 b28
    vinserti32x8         m1, m8, ym1, 1      ; a04 a12 b04 b12
    vshufi32x4           m8, m6, m3, q3232   ; c20 c28 d20 d28
    vinserti32x8         m6, ym3, 1          ; c04 c12 d04 d12
    vshufi32x4           m3, m1, m6, q3131   ; 12
    vshufi32x4           m1, m6, q2020       ;  4
    vshufi32x4           m6, m4, m2, q3131   ; 24
    vshufi32x4           m4, m2, q2020       ; 16
    vshufi32x4           m2, m0, m7, q3131   ;  8
    vshufi32x4           m0, m7, q2020       ;  0
    vshufi32x4           m7, m5, m8, q3131   ; 28
    vshufi32x4           m5, m8, q2020       ; 20
    vshufi32x4          m18, m14, m17, q3232 ; a18 a26 b18 b26
    vinserti32x8        m14, ym17, 1         ; a02 a10 b02 b10
    vshufi32x4          m17, m20, m13, q3232 ; c18 c26 d18 d26
    vinserti32x8        m20, ym13, 1         ; c02 c10 d02 d10
    vshufi32x4          m13, m21, m9, q3232  ; c22 c30 d22 d30
    vinserti32x8        m21, ym9, 1          ; c06 c14 d06 d14
    vshufi32x4          m19, m15, m16, q3232 ; a22 a30 b22 b30
    vinserti32x8        m15, ym16, 1         ; a06 a14 b06 b14
    vshufi32x4          m16, m14, m20, q3131 ; 10
    vshufi32x4          m14, m20, q2020      ;  2
    vshufi32x4          m20, m18, m17, q3131 ; 26
    vshufi32x4          m18, m17, q2020      ; 18
    vshufi32x4          m17, m15, m21, q3131 ; 14
    vshufi32x4          m15, m21, q2020      ;  6
    vshufi32x4          m21, m19, m13, q3131 ; 30
    vshufi32x4          m19, m13, q2020      ; 22
    call .idct16
    mova         [rsp+64*0], m14
    mova         [rsp+64*1], m15
    mova         [rsp+64*2], m16
    mova         [rsp+64*3], m17
    mova         [rsp+64*4], m18
    mova         [rsp+64*5], m19
    mova         [rsp+64*6], m20
    mova         [rsp+64*7], m21
    mova                m15, [rsp+64* 8]
    mova                m16, [rsp+64* 9]
    mova                m17, [rsp+64*10]
    mova                m19, [rsp+64*11]
    mova                m20, [rsp+64*12]
    mova                m21, [rsp+64*13]
    mova                m13, [rsp+64*14]
    mova                m18, [rsp+64*15]
    vshufi32x4          m14, m22, m15, q3232 ; a17 a25 b17 b25
    vinserti32x8        m22, ym15, 1         ; a01 a09 b01 b09
    vshufi32x4          m15, m23, m16, q3232 ; a19 a27 b19 b27
    vinserti32x8        m23, ym16, 1         ; a03 a11 b03 b11
    vshufi32x4          m16, m24, m17, q3232 ; a21 a29 b21 b29
    vinserti32x8        m24, ym17, 1         ; a05 a13 b05 b13
    vshufi32x4          m17, m25, m19, q3232 ; a23 a31 b23 b31
    vinserti32x8        m25, ym19, 1         ; a07 a15 b07 b15
    vinserti32x8         m8, m26, ym20, 1    ; c01 c09 d01 d09
    vshufi32x4          m26, m20, q3232      ; c17 c25 d17 d25
    vinserti32x8         m9, m27, ym21, 1    ; c03 c11 d03 d11
    vshufi32x4          m27, m21, q3232      ; c19 c27 d19 d27
    vinserti32x8        m11, m28, ym13, 1    ; c05 c13 d05 d13
    vshufi32x4          m28, m13, q3232      ; c21 c29 d21 d29
    vinserti32x8        m12, m29, ym18, 1    ; c07 c15 d07 d15
    vshufi32x4          m29, m18, q3232      ; c23 c31 d23 d31
    vshufi32x4          m18, m14, m26, q3131 ; 25
    vshufi32x4          m14, m26, q2020      ; 17
    vshufi32x4          m19, m15, m27, q3131 ; 27
    vshufi32x4          m15, m27, q2020      ; 19
    vshufi32x4          m20, m16, m28, q3131 ; 29
    vshufi32x4          m16, m28, q2020      ; 21
    vshufi32x4          m21, m17, m29, q3131 ; 31
    vshufi32x4          m17, m29, q2020      ; 23
    vshufi32x4          m26, m22, m8, q3131  ;  9
    vshufi32x4          m22, m8, q2020       ;  1
    vshufi32x4          m27, m23, m9, q3131  ; 11
    vshufi32x4          m23, m9, q2020       ;  3
    vshufi32x4          m28, m24, m11, q3131 ; 13
    vshufi32x4          m24, m11, q2020      ;  5
    vshufi32x4          m29, m25, m12, q3131 ; 15
    vshufi32x4          m25, m12, q2020      ;  7
    call .main
    jmp .end
.fast:
    mova                m14, [o(dup16_perm)]
    pmovzxbw             m9, [cq+64*0]
    pmovzxbw             m6, [cq+64*8]
    vpermb               m8, m14, [cq+64* 2]
    vpermb               m0, m14, [cq+64*14]
    vpermb               m5, m14, [cq+64*10]
    vpermb               m1, m14, [cq+64* 6]
    vpermb               m7, m14, [cq+64* 4]
    vpermb               m3, m14, [cq+64*12]
    vpbroadcastd        m10, [o(pd_8192)]
    vpbroadcastq        m13, [o(int_mshift)]
    packuswb             m9, m9
    packuswb             m6, m6
    vpcmpub              k7, m13, m10, 6
    IDCT16_MAIN           1
    vpermb              m21, m14, [cq+64* 1]
    vpermb              m17, m14, [cq+64*15]
    vpermb              m20, m14, [cq+64* 9]
    vpermb              m15, m14, [cq+64* 7]
    vpermb              m18, m14, [cq+64* 5]
    vpermb              m16, m14, [cq+64*11]
    vpermb              m19, m14, [cq+64*13]
    vpermb              m14, m14, [cq+64* 3]
    call .main_packed_fast
    punpcklwd            m8, m0, m2
    punpckhwd            m0, m2
    punpcklwd            m2, m1, m3
    punpckhwd            m1, m3
    punpcklwd            m3, m4, m6
    punpckhwd            m4, m6
    punpcklwd            m6, m5, m7
    punpckhwd            m5, m7
    punpcklwd            m7, m14, m16
    punpckhwd           m14, m16
    punpcklwd           m16, m15, m17
    punpckhwd           m15, m17
    punpcklwd           m17, m19, m21
    punpckhwd           m19, m21
    punpckhwd           m21, m18, m20
    punpcklwd           m18, m20
    punpcklwd           m20, m8, m1
    punpckhwd            m8, m1
    punpcklwd            m1, m0, m2
    punpckhwd            m0, m2
    punpcklwd            m2, m3, m5
    punpckhwd            m3, m5
    punpcklwd            m5, m4, m6
    punpckhwd            m4, m6
    punpcklwd            m6, m7, m15
    punpckhwd            m7, m15
    punpcklwd           m15, m14, m16
    punpckhwd           m14, m16
    punpckhwd           m16, m18, m19
    punpcklwd           m18, m19
    punpcklwd           m19, m21, m17
    punpckhwd           m21, m17
    punpcklwd           m17, m8, m0         ; a2   a6   aa   ae
    punpckhwd            m8, m0             ; a3   a7   ab   af
    punpcklwd            m0, m20, m1        ; a0   a4   a8   ac
    punpckhwd           m20, m1             ; a1   a5   a9   ad
    punpcklwd            m1, m2, m5         ; b0   b4   b8   bc
    punpckhwd            m2, m5             ; b1   b5   b9   bd
    punpcklwd            m5, m3, m4         ; b2   b6   ba   be
    punpckhwd            m3, m4             ; b3   b7   bb   bf
    punpcklwd            m4, m6, m15        ; c0   c4   c8   cc
    punpckhwd            m6, m15            ; c1   c5   c9   cd
    punpcklwd           m15, m7, m14        ; c2   c6   ca   ce
    punpckhwd            m7, m14            ; c3   c7   cb   cf
    punpcklwd           m14, m18, m19       ; d0   d4   d8   dc
    punpckhwd           m18, m19            ; d1   d5   d9   dd
    punpcklwd            m9, m16, m21       ; d2   d6   da   de
    punpckhwd           m16, m21            ; d3   d7   db   df
    mov                 r3d, 64*12
    pxor               ym21, ym21
.fast_zero_loop:
    mova       [cq+r3+64*0], ym21
    mova       [cq+r3+64*1], ym21
    mova       [cq+r3+64*2], ym21
    mova       [cq+r3+64*3], ym21
    sub                 r3d, 64*4
    jge .fast_zero_loop
    vshufi32x4          m21, m0, m1, q3232  ; a8   ac   b8   bc
    vinserti32x8         m0, ym1, 1         ; a0   a4   b0   b4
    vinserti32x8         m1, m17, ym5, 1    ; a2   a6   b2   b6
    vshufi32x4           m5, m17, m5, q3232 ; aa   ae   ba   be
    vinserti32x8        m17, m8, ym3, 1     ; a3   a7   b3   b7
    vshufi32x4          m19, m8, m3, q3232  ; ab   af   bb   bf
    vinserti32x8         m3, m4, ym14, 1    ; c0   c4   d0   d4
    vshufi32x4           m4, m14, q3232     ; c8   cc   d8   dc
    vinserti32x8        m14, m20, ym2, 1    ; a1   a5   b1   b5
    vshufi32x4          m20, m2, q3232      ; a9   ad   b9   bd
    vinserti32x8         m2, m6, ym18, 1    ; c1   c5   d1   d5
    vshufi32x4           m6, m18, q3232     ; c9   cd   d9   dd
    vinserti32x8        m18, m15, ym9, 1    ; c2   c6   d2   d6
    vshufi32x4          m15, m9, q3232      ; ca   ce   da   de
    vinserti32x8         m9, m7, ym16, 1    ; c3   c7   d3   d7
    vshufi32x4           m7, m16, q3232     ; cb   cf   db   df
    vshufi32x4          m22, m14, m2, q2020 ;  1
    vshufi32x4          m24, m14, m2, q3131 ;  5
    vshufi32x4          m23, m17, m9, q2020 ;  3
    vshufi32x4          m25, m17, m9, q3131 ;  7
    vshufi32x4          m16, m5, m15, q2020 ; 10
    vshufi32x4          m17, m5, m15, q3131 ; 14
    vshufi32x4          m14, m1, m18, q2020 ;  2
    vshufi32x4          m15, m1, m18, q3131 ;  6
    vshufi32x4           m1, m0, m3, q3131  ;  4
    vshufi32x4           m0, m3, q2020      ;  0
    vshufi32x4           m3, m21, m4, q3131 ; 12
    vshufi32x4           m2, m21, m4, q2020 ;  8
    vshufi32x4          m26, m20, m6, q2020 ;  9
    vshufi32x4          m28, m20, m6, q3131 ; 13
    vshufi32x4          m27, m19, m7, q2020 ; 11
    vshufi32x4          m29, m19, m7, q3131 ; 15
    call .idct16_fast
    mova         [rsp+64*0], m14
    mova         [rsp+64*1], m15
    mova         [rsp+64*2], m16
    mova         [rsp+64*3], m17
    mova         [rsp+64*4], m18
    mova         [rsp+64*5], m19
    mova         [rsp+64*6], m20
    mova         [rsp+64*7], m21
    call .main_fast
.end:
    lea                  r4, [strideq*3]
    vpbroadcastd        m12, [o(pw_512)]
    movshdup            m13, [o(itx_perm)]
    lea                  r3, [dstq+r4*8]
    lea                  r5, [strideq+r4] ; stride*4
    add                  r3, r5           ; dst+stride*28
    IDCT_32x32_END       29,  0, strideq*0, r4
    IDCT_32x32_END       28,  1, strideq*1, strideq*2
    IDCT_32x32_END       27,  2, strideq*2, strideq*1
    IDCT_32x32_END       26,  3, r4       , strideq*0
    IDCT_32x32_END       25,  4, strideq*0, r4
    IDCT_32x32_END       24,  5, strideq*1, strideq*2
    IDCT_32x32_END       23,  6, strideq*2, strideq*1
    IDCT_32x32_END       22,  7, r4       , strideq*0
    IDCT_32x32_END       21,  8, strideq*0, r4
    IDCT_32x32_END       20,  9, strideq*1, strideq*2
    IDCT_32x32_END       19, 10, strideq*2, strideq*1
    IDCT_32x32_END       18, 11, r4       , strideq*0
    IDCT_32x32_END       17, 12, strideq*0, r4
    IDCT_32x32_END       16, 13, strideq*1, strideq*2
    IDCT_32x32_END       15, 14, strideq*2, strideq*1
    IDCT_32x32_END       14, 15, r4       , strideq*0
    RET
ALIGN function_align
.idct16_fast:
    vpbroadcastd        m21, [o(pw_16305x2)]
    vpbroadcastd         m8, [o(pw_1606x2)]
    vpbroadcastd        m18, [o(pw_m10394x2)]
    vpbroadcastd         m9, [o(pw_12665x2)]
    pmulhrsw            m21, m14 ; t15a
    vpbroadcastd        m19, [o(pw_14449x2)]
    pmulhrsw            m14, m8  ; t8a
    vpbroadcastd         m8, [o(pw_7723x2)]
    pmulhrsw            m18, m17 ; t9a
    vpbroadcastd        m20, [o(pw_m4756x2)]
    pmulhrsw            m17, m9  ; t14a
    vpbroadcastd         m9, [o(pw_15679x2)]
    pmulhrsw            m19, m16 ; t13a
    vpbroadcastd         m5, [o(pw_m9102x2)]
    pmulhrsw            m16, m8  ; t10a
    vpbroadcastd         m8, [o(pw_13623x2)]
    pmulhrsw            m20, m15 ; t11a
    vpbroadcastd         m7, [o(pw_16069x2)]
    pmulhrsw            m15, m9  ; t12a
    vpbroadcastd         m9, [o(pw_3196x2)]
    pmulhrsw             m5, m3  ; t5a
    vpbroadcastd         m6, [o(pw_15137x2)]
    pmulhrsw             m3, m8  ; t6a
    vpbroadcastd         m8, [o(pw_6270x2)]
    pmulhrsw             m7, m1  ; t7a
    vpbroadcastd         m4, [o(pw_11585x2)]
    pmulhrsw             m1, m9  ; t4
    vpbroadcastd        m10, [o(pd_8192)]
    pmulhrsw             m6, m2  ; t3
    pmulhrsw             m2, m8  ; t2
    pmulhrsw             m4, m0  ; t0
    mova                 m0, m4  ; t1
    jmp .idct16b
ALIGN function_align
.idct16:
    vpbroadcastd        m10, [o(pd_8192)]
    ITX_MULSUB_2W        14, 21, 8, 9, 10,  1606, 16305 ; t8a,  t15a
    ITX_MULSUB_2W        18, 17, 8, 9, 10, 12665, 10394 ; t9a,  t14a
    ITX_MULSUB_2W        16, 19, 8, 9, 10,  7723, 14449 ; t10a, t13a
    ITX_MULSUB_2W        20, 15, 8, 9, 10, 15679,  4756 ; t11a, t12
    ITX_MULSUB_2W         5,  3, 8, 9, 10, 13623,  9102 ; t5a, t6a
    ITX_MULSUB_2W         1,  7, 8, 9, 10,  3196, 16069 ; t4a, t7a
    ITX_MULSUB_2W         2,  6, 8, 9, 10,  6270, 15137 ; t2, t3
    ITX_MULSUB_2W         0,  4, 8, 9, 10, 11585, 11585 ; t1, t0
.idct16b:
    paddw                m8, m20, m16 ; t11
    psubw               m20, m16      ; t10
    paddw               m16, m15, m19 ; t12
    psubw               m15, m19      ; t13
    psubw               m19, m14, m18 ; t9
    paddw               m14, m18      ; t8
    psubw               m18, m21, m17 ; t14
    paddw               m21, m17      ; t15
    vpbroadcastd        m11, [o(pw_6270_15137)]
    vpbroadcastd        m12, [o(pw_m15137_6270)]
    ITX_MULSUB_2W        18, 19, 9, 17, 10, 11, 12 ; t9a,  t14a
    vpbroadcastd        m11, [o(pw_m6270_m15137)]
    ITX_MULSUB_2W        15, 20, 9, 17, 10, 12, 11 ; t10a, t13a
    vpbroadcastd        m11, [o(pw_11585_11585)]
    vpbroadcastd        m12, [o(pw_m11585_11585)]
    paddw                m9, m7, m3   ; t7
    psubw                m3, m7, m3   ; t6a
    paddw                m7, m1, m5   ; t4
    psubw                m1, m5       ; t5a
    psubw               m17, m14, m8  ; t11a
    paddw                m8, m14      ; t8a
    paddw               m14, m18, m15 ; t9
    psubw               m18, m15      ; t10
    psubw               m15, m19, m20 ; t13
    paddw               m19, m20      ; t14
    paddw               m20, m21, m16 ; t15a
    psubw               m16, m21, m16 ; t12a
    ITX_MULSUB_2W         3,  1, 5, 21, 10, 11, 12 ; t5,   t6
    ITX_MULSUB_2W        15, 18, 5, 21, 10, 11, 12 ; t10a, t13a
    ITX_MULSUB_2W        16, 17, 5, 21, 10, 11, 12 ; t11,  t12
    psubw                m5, m0, m2   ; t2
    paddw                m2, m0       ; t1
    paddw                m0, m4, m6   ; t0
    psubw                m4, m6       ; t3
    psubw                m6, m2, m1   ; t6
    paddw                m1, m2       ; t1
    paddw                m2, m5, m3   ; t2
    psubw                m5, m3       ; t5
    paddw                m3, m4, m7   ; t3
    psubw                m4, m7       ; t4
    psubw                m7, m0, m9   ; t7
    paddw                m0, m9       ; t0
    psubw               m21, m0, m20  ; out15
    paddw                m0, m20      ; out0
    psubw               m20, m1, m19  ; out14
    paddw                m1, m19      ; out1
    psubw               m19, m2, m18  ; out13
    paddw                m2, m18      ; out2
    psubw               m18, m3, m17  ; out12
    paddw                m3, m17      ; out3
    psubw               m17, m4, m16  ; out11
    paddw                m4, m16      ; out4
    psubw               m16, m5, m15  ; out10
    paddw                m5, m15      ; out5
    psubw               m15, m6, m14  ; out9
    paddw                m6, m14      ; out6
    psubw               m14, m7, m8   ; out8
    paddw                m7, m8       ; out7
    ret
ALIGN function_align
.main_fast:
    vpbroadcastd        m21, [o(pw_16364x2)]
    vpbroadcastd         m8, [o(pw_804x2)]
    vpbroadcastd        m14, [o(pw_m11003x2)]
    vpbroadcastd         m9, [o(pw_12140x2)]
    pmulhrsw            m21, m22 ; t31a
    vpbroadcastd        m17, [o(pw_14811x2)]
    pmulhrsw            m22, m8  ; t16a
    vpbroadcastd         m8, [o(pw_7005x2)]
    pmulhrsw            m14, m29 ; t30a
    vpbroadcastd        m18, [o(pw_m5520x2)]
    pmulhrsw            m29, m9  ; t17a
    vpbroadcastd         m9, [o(pw_15426x2)]
    pmulhrsw            m17, m26 ; t29a
    vpbroadcastd        m19, [o(pw_15893x2)]
    pmulhrsw            m26, m8  ; t18a
    vpbroadcastd         m8, [o(pw_3981x2)]
    pmulhrsw            m18, m25 ; t19a
    vpbroadcastd        m16, [o(pw_m8423x2)]
    pmulhrsw            m25, m9  ; t28a
    vpbroadcastd         m9, [o(pw_14053x2)]
    pmulhrsw            m19, m24 ; t27a
    vpbroadcastd        m15, [o(pw_13160x2)]
    pmulhrsw            m24, m8  ; t20a
    vpbroadcastd         m8, [o(pw_9760x2)]
    pmulhrsw            m16, m27 ; t21a
    vpbroadcastd        m20, [o(pw_m2404x2)]
    pmulhrsw            m27, m9  ; t26a
    vpbroadcastd         m9, [o(pw_16207x2)]
    pmulhrsw            m15, m28 ; t25a
    pmulhrsw            m28, m8  ; t22a
    pmulhrsw            m20, m23 ; t23a
    pmulhrsw            m23, m9  ; t24a
    jmp .main2
ALIGN function_align
.main:
    ITX_MULSUB_2W        22, 21,  8,  9, 10,   804, 16364 ; t16a, t31a
    ITX_MULSUB_2W        14, 29,  8,  9, 10, 12140, 11003 ; t17a, t30a
    ITX_MULSUB_2W        26, 17,  8,  9, 10,  7005, 14811 ; t18a, t29a
    ITX_MULSUB_2W        18, 25,  8,  9, 10, 15426,  5520 ; t19a, t28a
    ITX_MULSUB_2W        24, 19,  8,  9, 10,  3981, 15893 ; t20a, t27a
    ITX_MULSUB_2W        16, 27,  8,  9, 10, 14053,  8423 ; t21a, t26a
    ITX_MULSUB_2W        28, 15,  8,  9, 10,  9760, 13160 ; t22a, t25a
    ITX_MULSUB_2W        20, 23,  8,  9, 10, 16207,  2404 ; t23a, t24a
.main2:
    psubw                m8, m22, m14 ; t17
    paddw               m22, m14      ; t16
    paddw               m14, m18, m26 ; t19
    psubw               m18, m26      ; t18
    psubw               m26, m24, m16 ; t21
    paddw               m24, m16      ; t20
    psubw               m16, m20, m28 ; t22
    paddw               m28, m20      ; t23
    psubw               m20, m23, m15 ; t25
    paddw               m23, m15      ; t24
    psubw               m15, m21, m29 ; t30
    paddw               m21, m29      ; t31
    psubw               m29, m19, m27 ; t26
    paddw               m19, m27      ; t27
    paddw               m27, m25, m17 ; t28
    psubw               m25, m17      ; t29
    ITX_MULSUB_2W        15,  8,  9, 17, 10,   3196, 16069 ; t17a, t30a
    ITX_MULSUB_2W        25, 18,  9, 17, 10, m16069,  3196 ; t18a, t29a
    ITX_MULSUB_2W        29, 26,  9, 17, 10,  13623,  9102 ; t21a, t26a
    ITX_MULSUB_2W        20, 16,  9, 17, 10,  m9102, 13623 ; t22a, t25a
    psubw               m17, m21, m27 ; t28a
    paddw               m21, m27      ; t31a
    psubw               m27, m15, m25 ; t18
    paddw               m15, m25      ; t17
    psubw               m25, m20, m29 ; t21
    paddw               m20, m29      ; t22
    psubw               m29, m8, m18  ; t29
    paddw                m8, m18      ; t30
    psubw               m18, m22, m14 ; t19a
    paddw               m22, m14      ; t16a
    psubw               m14, m28, m24 ; t20a
    paddw               m24, m28      ; t23a
    paddw               m28, m16, m26 ; t25
    psubw               m16, m26      ; t26
    psubw               m26, m23, m19 ; t27a
    paddw               m23, m19      ; t24a
    vpbroadcastd        m12, [o(pw_m15137_6270)]
    vpbroadcastd        m11, [o(pw_6270_15137)]
    ITX_MULSUB_2W        29, 27,  9, 19, 10, 11, 12 ; t18a, t29a
    ITX_MULSUB_2W        17, 18,  9, 19, 10, 11, 12 ; t19,  t28
    vpbroadcastd        m11, [o(pw_m6270_m15137)]
    ITX_MULSUB_2W        16, 25,  9, 19, 10, 12, 11 ; t21a, t26a
    ITX_MULSUB_2W        26, 14,  9, 19, 10, 12, 11 ; t20,  t27
    vpbroadcastd        m12, [o(pw_m11585_11585)]
    vpbroadcastd        m11, [o(pw_11585_11585)]
    psubw               m19, m27, m25 ; t26
    paddw               m27, m25      ; t29
    psubw               m25, m17, m26 ; t20a
    paddw               m17, m26      ; t19a
    paddw               m26, m18, m14 ; t28a
    psubw               m18, m14      ; t27a
    paddw               m14, m22, m24 ; t16
    psubw               m22, m24      ; t23
    psubw               m24, m29, m16 ; t21
    paddw               m16, m29      ; t18
    paddw               m29, m21, m23 ; t31
    psubw               m21, m23      ; t24
    psubw               m23, m15, m20 ; t22a
    paddw               m15, m20      ; t17a
    psubw               m20, m8, m28  ; t25a
    paddw               m28, m8       ; t30a
    ITX_MULSUB_2W        18, 25,  8,  9, 10, 11, 12 ; t20,  t27
    ITX_MULSUB_2W        19, 24,  8,  9, 10, 11, 12 ; t21a, t26a
    ITX_MULSUB_2W        21, 22,  8,  9, 10, 11, 12 ; t23a, t24a
    ITX_MULSUB_2W        20, 23,  8,  9, 10, 11, 12 ; t22,  t25
    ret
ALIGN function_align
.main_packed_fast:
    vpbroadcastd         m8, [o(pw_804_16364x2)]
    vpbroadcastd         m9, [o(pw_m11003_12140x2)]
    vpbroadcastd        m11, [o(pw_7005_14811x2)]
    vpbroadcastd        m12, [o(pw_m5520_15426x2)]
    pmulhrsw            m21, m8       ; t16a, t31a
    vpbroadcastd         m8, [o(pw_3981_15893x2)]
    pmulhrsw            m17, m9       ; t17a, t30a
    vpbroadcastd         m9, [o(pw_m8423_14053x2)]
    pmulhrsw            m20, m11      ; t18a, t29a
    vpbroadcastd        m11, [o(pw_9760_13160x2)]
    pmulhrsw            m15, m12      ; t19a, t28a
    vpbroadcastd        m12, [o(pw_m2404_16207x2)]
    pmulhrsw            m18, m8       ; t20a, t27a
    pmulhrsw            m16, m9       ; t21a, t26a
    pmulhrsw            m19, m11      ; t22a, t25a
    pmulhrsw            m14, m12      ; t23a, t24a
    psubw                m8, m21, m17 ; t17 t30
    paddw               m21, m17      ; t16 t31
    psubw               m17, m15, m20 ; t18 t29
    paddw               m20, m15      ; t19 t28
    psubw               m15, m18, m16 ; t21 t26
    paddw               m18, m16      ; t20 t27
    psubw               m16, m14, m19 ; t22 t25
    paddw               m14, m19      ; t23 t24
    ITX_MUL2X_PACK        8, 9, 19, 10,   3196, 16069, 5 ; t17a t30a
    ITX_MUL2X_PACK       17, 9, 19, 10, m16069,  3196, 5 ; t18a t29a
    ITX_MUL2X_PACK       15, 9, 19, 10,  13623,  9102, 5 ; t21a t26a
    ITX_MUL2X_PACK       16, 9, 19, 10,  m9102, 13623, 5 ; t22a t25a
    vpbroadcastd        m11, [o(pw_m15137_6270)]
    psubw               m19, m21, m20 ; t19a t28a
    paddw               m21, m20      ; t16a t31a
    psubw               m20, m14, m18 ; t20a t27a
    paddw               m14, m18      ; t23a t24a
    psubw               m18, m8, m17  ; t18  t29
    paddw                m8, m17      ; t17  t30
    psubw               m17, m16, m15 ; t21  t26
    paddw               m15, m16      ; t22  t25
    ITX_MUL2X_PACK       18, 9, 16, 10, 6270_15137, 11,   20 ; t18a t29a
    ITX_MUL2X_PACK       19, 9, 16, 10, 6270_15137, 11,   20 ; t19  t28
    ITX_MUL2X_PACK       20, 9, 16, 10, 11, m6270_m15137, 36 ; t20  t27
    ITX_MUL2X_PACK       17, 9, 16, 10, 11, m6270_m15137, 36 ; t21a t26a
    vbroadcasti32x4      m9, [o(deint_shuf)]
    psubw               m16, m21, m14 ; t23  t24
    paddw               m14, m21      ; t16  t31
    psubw               m21, m8, m15  ; t22a t25a
    paddw               m15, m8       ; t17a t30a
    psubw                m8, m18, m17 ; t21  t26
    paddw               m18, m17      ; t18  t29
    paddw               m17, m19, m20 ; t19a t28a
    psubw               m19, m20      ; t20a t27a
    vpbroadcastd        m11, [o(pw_m11585_11585)]
    vpbroadcastd        m12, [o(pw_11585_11585)]
    REPX     {pshufb x, m9}, m14, m15, m18, m17
    mova                 m9, m10
    vpdpwssd             m9, m16, m11
    mova                m20, m10
    vpdpwssd            m20, m21, m11
    psrad                m9, 14
    psrad               m20, 14
    packssdw             m9, m20      ; t23a t22
    mova                m20, m10
    vpdpwssd            m20, m16, m12
    mova                m16, m10
    vpdpwssd            m16, m21, m12
    psrad               m20, 14
    psrad               m16, 14
    packssdw            m16, m20, m16 ; t24a t25
    ITX_MUL2X_PACK        8, 21, 20, 10, 11, 12, 8 ; t21a t26a
    ITX_MUL2X_PACK       19,  8, 11, 10, 11, 12, 8 ; t20  t27
    packssdw            m11, m20      ; t27  t26a
    packssdw             m8, m21      ; t20  t21a
    punpcklqdq          m20, m14, m15 ; t16  t17a
    punpckhqdq          m14, m15      ; t31  t30a
    punpckhqdq          m15, m17, m18 ; t28a t29
    punpcklqdq          m17, m18      ; t19a t18
    psubw               m21, m0, m14  ; out31 out30
    paddw                m0, m14      ; out0  out1
    psubw               m14, m7, m20  ; out16 out17
    paddw                m7, m20      ; out15 out14
    psubw               m20, m1, m15  ; out28 out29
    paddw                m1, m15      ; out3  out2
    psubw               m15, m6, m17  ; out19 out18
    paddw                m6, m17      ; out12 out13
    psubw               m17, m4, m9   ; out23 out22
    paddw                m4, m9       ; out8  out9
    psubw               m18, m3, m16  ; out24 out25
    paddw                m3, m16      ; out7  out6
    psubw               m16, m5, m8   ; out20 out21
    paddw                m5, m8       ; out11 out10
    psubw               m19, m2, m11  ; out27 out26
    paddw                m2, m11      ; out4  out5
    ret

%endif
