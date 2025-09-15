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

%if ARCH_X86_64 && HAVE_AVX2_EXTERNAL

SECTION_RODATA 16

deint_shuf:  db  0,  1,  4,  5,  8,  9, 12, 13,  2,  3,  6,  7, 10, 11, 14, 15

pw_512:      times 2 dw 512  ; 16-byte aligned
pd_8192:     dd 8192
pw_m512:     times 2 dw -512
pw_2048:     times 2 dw 2048
pw_1024:     times 2 dw 1024 ; 16-byte aligned

pw_804x2:    times 2 dw    804*2
pw_1606x2:   times 2 dw   1606*2
pw_3196x2:   times 2 dw   3196*2
pw_3981x2:   times 2 dw   3981*2
pw_6270x2:   times 2 dw   6270*2
pw_7005x2:   times 2 dw   7005*2
pw_7723x2:   times 2 dw   7723*2
pw_9760x2:   times 2 dw   9760*2
pw_11585x2:  times 2 dw  11585*2
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
pw_m11585x2: times 2 dw -11585*2

%macro COEF_PAIR 2-3
pw_%1_%2:   dw  %1, %2
pw_m%2_%1:  dw -%2, %1
%if %0 == 3
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
COEF_PAIR 11585, 11585
COEF_PAIR 12140, 11003
COEF_PAIR 12665, 10394
COEF_PAIR 13623,  9102, 1
COEF_PAIR 14053,  8423
COEF_PAIR 15137,  6270
COEF_PAIR 15426,  5520
COEF_PAIR 15679,  4756
COEF_PAIR 16069,  3196
COEF_PAIR 16207,  2404

; ADST4-only:
pw_0_13377:      dw      0,  13377
pw_13377_m13377: dw  13377, -13377
pw_m13377_m5283: dw -13377,  -5283
pw_13377_m15212: dw  13377, -15212
pw_9929_m5283:   dw   9929,  -5283
pw_5283_15212:   dw   5283,  15212
pw_13377_9929:   dw  13377,   9929

; ADST16-only:
pw_8423_3981:     dw   8423,   3981
pw_m8423_3981:    dw  -8423,   3981
pw_14053_m15893:  dw  14053, -15893
pw_m14053_m15893: dw -14053, -15893
pw_2404_9760:     dw   2404,   9760
pw_m2404_9760:    dw  -2404,   9760
pw_16207_m13160:  dw  16207, -13160
pw_m16207_m13160: dw -16207, -13160
pw_11003_804:     dw  11003,    804
pw_m11003_804:    dw -11003,    804
pw_12140_m16364:  dw  12140, -16364
pw_m12140_m16364: dw -12140, -16364
pw_5520_7005:     dw   5520,   7005
pw_m5520_7005:    dw  -5520,   7005
pw_15426_m14811:  dw  15426, -14811
pw_m15426_m14811: dw -15426, -14811

SECTION .text

%define o_base pw_512 + 128
%define o(x) (r6 - (o_base) + (x))
%define m(x) mangle(private_prefix %+ _ %+ x %+ SUFFIX)

%macro IWHT4_1D_PACKED 0
    psubw                m2, m0, m3
    paddw                m0, m3
    punpckhqdq           m2, m2     ; t2 t2
    punpcklqdq           m0, m0     ; t0 t0
    psubw                m1, m0, m2
    psraw                m1, 1
    psubw                m1, m3     ; t1 t3
    psubw                m0, m1     ; ____ out0
    paddw                m2, m1     ; out3 ____
%endmacro

INIT_XMM avx2
cglobal vp9_iwht_iwht_4x4_add, 3, 3, 6, dst, stride, c
    mova                 m0, [cq+16*0]
    mova                 m1, [cq+16*1]
    pxor                 m2, m2
    mova          [cq+16*0], m2
    mova          [cq+16*1], m2
    lea                  r2, [dstq+strideq*2]
    punpckhqdq           m3, m0, m1 ; in1 in3
    punpcklqdq           m0, m1     ; in0 in2
    movd                 m4, [r2  +strideq*0]
    pinsrd               m4, [dstq+strideq*1], 1
    movd                 m5, [r2  +strideq*1]
    pinsrd               m5, [dstq+strideq*0], 1
    psraw                m3, 2
    psraw                m0, 2
    IWHT4_1D_PACKED
    punpckhwd            m0, m1
    punpcklwd            m1, m2
    punpckhdq            m2, m0, m1
    punpckldq            m0, m1
    punpckhqdq           m3, m0, m2
    punpcklqdq           m0, m2
    IWHT4_1D_PACKED
    pmovzxbw             m4, m4
    pmovzxbw             m5, m5
    vpblendd             m0, m2, 0x03
    paddw                m1, m4
    paddw                m0, m5
    packuswb             m0, m1
    pextrd [dstq+strideq*0], m0, 1
    pextrd [dstq+strideq*1], m0, 3
    pextrd [r2  +strideq*0], m0, 2
    movd   [r2  +strideq*1], m0
    RET

%macro ITX_MUL2X_PACK 6-7 0 ; dst/src, tmp[1-2], rnd, coef[1-2], swap
%if %7
    vpbroadcastd        m%2, [o(pw_%5_%6)]
    vpbroadcastd        m%3, [o(pw_m%6_%5)]
%else
    vpbroadcastd        m%2, [o(pw_m%6_%5)]
    vpbroadcastd        m%3, [o(pw_%5_%6)]
%endif
    pmaddwd             m%2, m%1
    pmaddwd             m%1, m%3
    paddd               m%2, m%4
    paddd               m%1, m%4
    psrad               m%2, 14
    psrad               m%1, 14
    packssdw            m%1, m%2
%endmacro

; dst1 = (src1 * coef1 - src2 * coef2 + rnd) >> 12
; dst2 = (src1 * coef2 + src2 * coef1 + rnd) >> 12
%macro ITX_MULSUB_2W 7 ; dst/src[1-2], tmp[1-2], rnd, coef[1-2]
    punpckhwd           m%3, m%2, m%1
    punpcklwd           m%2, m%1
%if %7 < 32
    pmaddwd             m%1, m%7, m%2
    pmaddwd             m%4, m%7, m%3
%else
    vpbroadcastd        m%1, [o(pw_m%7_%6)]
    pmaddwd             m%4, m%3, m%1
    pmaddwd             m%1, m%2
%endif
    paddd               m%4, m%5
    paddd               m%1, m%5
    psrad               m%4, 14
    psrad               m%1, 14
    packssdw            m%1, m%4
%if %7 < 32
    pmaddwd             m%3, m%6
    pmaddwd             m%2, m%6
%else
    vpbroadcastd        m%4, [o(pw_%6_%7)]
    pmaddwd             m%3, m%4
    pmaddwd             m%2, m%4
%endif
    paddd               m%3, m%5
    paddd               m%2, m%5
    psrad               m%3, 14
    psrad               m%2, 14
    packssdw            m%2, m%3
%endmacro

%macro ADST_MULSUB_2W 8-10 ; dst/src[1-2], dst[3-4], tmp, rnd, coef[1-4]
    vpbroadcastd        m%3, [o(pw_m%8_%7)]
    vpbroadcastd        m%4, [o(pw_%7_%8)]
    pmaddwd             m%3, m%1
    pmaddwd             m%1, m%4
%if %0 == 8
    vpbroadcastd        m%5, [o(pw_%8_%7)]
    vpbroadcastd        m%4, [o(pw_m%7_%8)]
%else
    vpbroadcastd        m%5, [o(pw_m%10_%9)]
    vpbroadcastd        m%4, [o(pw_%9_%10)]
%endif
    pmaddwd             m%5, m%2
    pmaddwd             m%4, m%2
    paddd               m%3, m%6
    paddd               m%1, m%6
    psubd               m%2, m%1, m%4
    paddd               m%1, m%4
    psubd               m%4, m%3, m%5
    paddd               m%3, m%5
%endmacro

%macro ADST_MULSUB_4W 12-14 ; dst/src[1-4], tmp[1-5], rnd, coef[1-4]
    vpbroadcastd        m%8, [o(pw_%11_%12)]
    vpbroadcastd        m%7, [o(pw_m%12_%11)]
    punpckhwd           m%5, m%2, m%1
    punpcklwd           m%2, m%1
%if %0 == 12
    vpbroadcastd        m%1, [o(pw_m%11_%12)]
    vpbroadcastd        m%9, [o(pw_%12_%11)]
%else
    vpbroadcastd        m%1, [o(pw_%13_%14)]
    vpbroadcastd        m%9, [o(pw_m%14_%13)]
%endif
    pmaddwd             m%6, m%5, m%8
    pmaddwd             m%8, m%2
    pmaddwd             m%5, m%7
    pmaddwd             m%2, m%7
    punpckhwd           m%7, m%4, m%3
    punpcklwd           m%4, m%3
    pmaddwd             m%3, m%7, m%1
    pmaddwd             m%1, m%4
    pmaddwd             m%7, m%9
    pmaddwd             m%9, m%4
    REPX    {paddd x, m%10}, m%6, m%8, m%5, m%2
    psubd               m%4, m%6, m%3
    paddd               m%6, m%3
    psubd               m%3, m%8, m%1
    paddd               m%1, m%8
    REPX      {psrad x, 14}, m%4, m%3, m%6, m%1
    psubd               m%8, m%5, m%7
    paddd               m%5, m%7
    packssdw            m%3, m%4
    psubd               m%4, m%2, m%9
    paddd               m%2, m%9
    packssdw            m%1, m%6
    REPX      {psrad x, 14}, m%8, m%4, m%5, m%2
    packssdw            m%4, m%8
    packssdw            m%2, m%5
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

%macro INV_TXFM_4X4_FN 2 ; type1, type2
    INV_TXFM_FN          %1, %2, 4x4
%ifidn %1_%2, dct_dct
    vpbroadcastw         m0, [cq]
    vpbroadcastd         m1, [o(pw_11585x2)]
    pmulhrsw             m0, m1
    pmulhrsw             m0, m1
    mova                 m1, m0
    jmp m(vp9_idct_4x4_internal).pass2_end
%endif
%endmacro

INV_TXFM_4X4_FN dct, dct
INV_TXFM_4X4_FN dct, adst

cglobal vp9_idct_4x4_internal, 0, 5, 6, dst, stride, c, eob, tx2
    mova                 m0, [cq+16*0]
    mova                 m1, [cq+16*1]
    call .main
.pass1_end:
    mova                 m2, [o(deint_shuf)]
    shufps               m3, m0, m1, q1331
    shufps               m0, m1, q0220
    pshufb               m0, m2
    pshufb               m1, m3, m2
    jmp                tx2q
.pass2:
    call .main
.pass2_end:
    vpbroadcastd         m2, [o(pw_2048)]
    pmulhrsw             m0, m2
    pmulhrsw             m1, m2
    lea                  r3, [dstq+strideq*2]
    movd                 m2, [dstq+strideq*0]
    pinsrd               m2, [dstq+strideq*1], 1
    movd                 m3, [r3  +strideq*1]
    pinsrd               m3, [r3  +strideq*0], 1
    pxor                 m4, m4
    pmovzxbw             m2, m2
    mova          [cq+16*0], m4
    pmovzxbw             m3, m3
    mova          [cq+16*1], m4
    paddw                m0, m2
    paddw                m1, m3
    packuswb             m0, m1
    movd   [dstq+strideq*0], m0
    pextrd [dstq+strideq*1], m0, 1
    pextrd [r3  +strideq*0], m0, 3
    pextrd [r3  +strideq*1], m0, 2
    RET
ALIGN function_align
.main:
    vpbroadcastd         m4, [o(pd_8192)]
    punpckhwd            m2, m1, m0
    psubw                m3, m0, m1
    paddw                m0, m1
    punpcklqdq           m0, m3
    ITX_MUL2X_PACK        2, 1, 3, 4, 6270, 15137
    vpbroadcastd         m4, [o(pw_11585x2)]
    pmulhrsw             m0, m4     ; t0 t1
    psubw                m1, m0, m2 ; out3 out2
    paddw                m0, m2     ; out0 out1
    ret

INV_TXFM_4X4_FN adst, dct
INV_TXFM_4X4_FN adst, adst

cglobal vp9_iadst_4x4_internal, 0, 5, 6, dst, stride, c, eob, tx2
    mova                 m0, [cq+16*0]
    mova                 m1, [cq+16*1]
    call .main
    jmp m(vp9_idct_4x4_internal).pass1_end
.pass2:
    call .main
    jmp m(vp9_idct_4x4_internal).pass2_end
ALIGN function_align
.main:
    vpbroadcastd         m4, [o(pw_0_13377)]
    punpckhwd            m2, m0, m1
    vpbroadcastd         m5, [o(pw_13377_m13377)]
    punpcklwd            m0, m1
    vpbroadcastd         m1, [o(pw_m13377_m5283)]
    vpbroadcastd         m3, [o(pw_13377_m15212)]
    pmaddwd              m4, m2
    pmaddwd              m5, m0
    pmaddwd              m1, m2
    pmaddwd              m3, m2
    paddd                m4, m5 ; 2
    vpbroadcastd         m5, [o(pw_9929_m5283)]
    pmaddwd              m5, m0
    paddd                m1, m5
    paddd                m3, m5 ; 1
    vpbroadcastd         m5, [o(pw_5283_15212)]
    pmaddwd              m0, m5
    vpbroadcastd         m5, [o(pw_13377_9929)]
    pmaddwd              m2, m5
    vpbroadcastd         m5, [o(pd_8192)]
    paddd                m4, m5
    paddd                m0, m5
    paddd                m3, m5
    paddd                m1, m0 ; 3
    paddd                m0, m2 ; 0
    REPX      {psrad x, 14}, m4, m1, m3, m0
    packssdw             m1, m4 ; out3 out2
    packssdw             m0, m3 ; out0 out1
    ret

%macro WRITE_8X4 4-7 strideq*1, strideq*2, r3 ; coefs[1-2], tmp[1-2], off[1-3]
    movq               xm%3, [dstq   ]
    movhps             xm%3, [dstq+%5]
    movq               xm%4, [dstq+%6]
    movhps             xm%4, [dstq+%7]
    pmovzxbw            m%3, xm%3
    pmovzxbw            m%4, xm%4
    paddw               m%3, m%1
    paddw               m%4, m%2
    packuswb            m%3, m%4
    vextracti128       xm%4, m%3, 1
    movq          [dstq   ], xm%3
    movhps        [dstq+%6], xm%3
    movq          [dstq+%5], xm%4
    movhps        [dstq+%7], xm%4
%endmacro

%macro INV_TXFM_8X8_FN 2 ; type1, type2
    INV_TXFM_FN          %1, %2, 8x8
%ifidn %1_%2, dct_dct
    vpbroadcastw        xm2, [cq]
    vpbroadcastd        xm1, [o(pw_11585x2)]
    vpbroadcastd        xm0, [o(pw_1024)]
    mov           word [cq], 0
    pmulhrsw            xm2, xm1
    add                 r3d, 3
    pmulhrsw            xm2, xm1
    pmulhrsw            xm2, xm0
.dconly_loop:
    pmovzxbw            xm0, [dstq+strideq*0]
    pmovzxbw            xm1, [dstq+strideq*1]
    paddw               xm0, xm2
    paddw               xm1, xm2
    packuswb            xm0, xm1
    movq   [dstq+strideq*0], xm0
    movhps [dstq+strideq*1], xm0
    lea                dstq, [dstq+strideq*2]
    dec                 r3d
    jg .dconly_loop
    RET
%endif
%endmacro

INIT_YMM avx2
INV_TXFM_8X8_FN dct, dct
INV_TXFM_8X8_FN dct, adst

cglobal vp9_idct_8x8_internal, 0, 5, 8, dst, stride, c, eob, tx2
    vpermq               m0, [cq+32*0], q3120 ; 0 1
    vpermq               m3, [cq+32*3], q3120 ; 6 7
    vpermq               m2, [cq+32*2], q3120 ; 4 5
    vpermq               m1, [cq+32*1], q3120 ; 2 3
    call .main
    shufps               m4, m0, m1, q0220
    shufps               m5, m0, m1, q1331
    vbroadcasti128       m0, [o(deint_shuf)]
    shufps               m1, m2, m3, q0220
    shufps               m3, m2, m3, q1331
    REPX     {pshufb x, m0}, m4, m5, m1, m3
    vinserti128          m0, m4, xm1, 1
    vperm2i128           m2, m4, m1, 0x31
    vinserti128          m1, m5, xm3, 1
    vperm2i128           m3, m5, m3, 0x31
    jmp                tx2q
.pass2:
    call .main
    vpbroadcastd         m4, [o(pw_1024)]
    vpermq               m1, m1, q2031
    vpermq               m3, m3, q2031
.end:
    vpermq               m0, m0, q3120
    vpermq               m2, m2, q3120
    REPX   {pmulhrsw x, m4}, m0, m1, m2, m3
    pxor                 m4, m4
    REPX {mova [cq+32*x], m4}, 0, 1, 2, 3
    lea                  r3, [strideq*3]
    WRITE_8X4             0, 1, 4, 5
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4             2, 3, 4, 5
    RET
ALIGN function_align
.main:
    vpbroadcastd         m6, [o(pd_8192)]
    punpckhwd            m5, m3, m0 ; in7 in1
    punpckhwd            m4, m1, m2 ; in3 in5
    punpcklwd            m3, m1     ; in2 in6
    psubw                m1, m0, m2
    paddw                m0, m2
    punpcklqdq           m0, m1     ; in0+in4 in0-in4
    ITX_MUL2X_PACK        5, 1, 2, 6,  3196, 16069, 1 ; t4a t7a
    ITX_MUL2X_PACK        4, 1, 2, 6, 13623,  9102, 1 ; t5a t6a
    ITX_MUL2X_PACK        3, 1, 2, 6,  6270, 15137    ; t3 t2
    vpbroadcastd         m6, [o(pw_11585x2)]
    psubw                m2, m5, m4 ; t4 t7
    paddw                m5, m4     ; t5a t6a
    pshufd               m4, m2, q1032
    psubw                m1, m2, m4
    paddw                m4, m2
    vpblendd             m4, m1, 0xcc
    pmulhrsw             m0, m6     ; t0 t1
    pmulhrsw             m4, m6     ; t6 t5
    psubw                m1, m0, m3 ; tmp3 tmp2
    paddw                m0, m3     ; tmp0 tmp1
    shufps               m2, m5, m4, q1032
    vpblendd             m5, m4, 0xcc
    psubw                m3, m0, m2 ; out7 out6
    paddw                m0, m2     ; out0 out1
    psubw                m2, m1, m5 ; out4 out5
    paddw                m1, m5     ; out3 out2
    ret

INV_TXFM_8X8_FN adst, dct
INV_TXFM_8X8_FN adst, adst

cglobal vp9_iadst_8x8_internal, 0, 5, 8, dst, stride, c, eob, tx2
    vpermq               m4, [cq+32*0], q1302 ; 1 0
    vpermq               m3, [cq+32*3], q3120 ; 6 7
    vpermq               m5, [cq+32*1], q1302 ; 3 2
    vpermq               m2, [cq+32*2], q3120 ; 4 5
    call .main
    punpcklwd            m4, m0, m1
    punpckhwd            m0, m1
    punpcklwd            m1, m2, m3
    punpckhwd            m2, m3
    pxor                 m3, m3
    psubw                m0, m3, m0
    psubw                m2, m3, m2
    punpcklwd            m3, m4, m0
    punpckhwd            m4, m0
    punpcklwd            m0, m1, m2
    punpckhwd            m1, m2
    vperm2i128           m2, m3, m0, 0x31
    vinserti128          m0, m3, xm0, 1
    vperm2i128           m3, m4, m1, 0x31
    vinserti128          m1, m4, xm1, 1
    jmp                tx2q
.pass2:
    pshufd               m4, m0, q1032
    pshufd               m5, m1, q1032
    call .main
    vpbroadcastd         m5, [o(pw_1024)]
    vpbroadcastd        xm4, [o(pw_2048)]
    psubw                m4, m5 ; lower half = 1024, upper half = -1024
    REPX {vpermq x, x, q3120}, m1, m3
    jmp m(vp9_idct_8x8_internal).end
ALIGN function_align
.main:
    vpbroadcastd         m7, [o(pd_8192)]
    punpckhwd            m0, m4, m3 ; 0 7
    punpckhwd            m1, m5, m2 ; 2 5
    punpcklwd            m2, m5     ; 4 3
    punpcklwd            m3, m4     ; 6 1
    ADST_MULSUB_2W        0, 2, 4, 5, 6, 7, 1606, 16305, 12665, 10394 ; t0, t4, t1, t5
    pslld                m2, 2
    REPX      {psrad x, 14}, m0, m5, m4
    pblendw              m2, m5, 0x55 ;  t5    t4
    packssdw             m0, m4       ;  t0    t1
    ADST_MULSUB_2W        1, 3, 4, 5, 6, 7, 7723, 14449, 15679,  4756 ; t2, t6, t3, t7
    pslld                m5, 2
    REPX      {psrad x, 14}, m3, m1, m4
    pblendw              m3, m5, 0xaa ;  t6    t7
    packssdw             m1, m4       ;  t2    t3
    ADST_MULSUB_2W        2, 3, 4, 5, 6, 7, 6270, 15137 ; t4, t6, t5, t7
    REPX      {psrad x, 14}, m3, m2, m5, m4
    packssdw             m3, m5       ;  t6    t7
    packssdw             m2, m4       ; -out1  out6
    vpbroadcastd         m5, [o(pw_11585x2)]
    psubw                m4, m0, m1   ;  t2    t3
    paddw                m0, m1       ;  out0 -out7
    punpckhqdq           m1, m4, m3   ;  t3    t7
    punpcklqdq           m4, m3       ;  t2    t6
    punpckhqdq           m3, m2, m0   ;  out6 -out7
    punpcklqdq           m0, m2       ;  out0 -out1
    psubw                m2, m4, m1
    paddw                m1, m4
    pshufd               m1, m1, q1032
    pmulhrsw             m2, m5       ;  out4 -out5
    pmulhrsw             m1, m5       ;  out2 -out3
    ret

%macro WRITE_16X2 6 ; coefs[1-2], tmp[1-2], offset[1-2]
    pmovzxbw            m%3, [dstq+%5]
%ifnum %1
    paddw               m%3, m%1
%else
    paddw               m%3, %1
%endif
    pmovzxbw            m%4, [dstq+%6]
%ifnum %2
    paddw               m%4, m%2
%else
    paddw               m%4, %2
%endif
    packuswb            m%3, m%4
    vpermq              m%3, m%3, q3120
    mova          [dstq+%5], xm%3
    vextracti128  [dstq+%6], m%3, 1
%endmacro

%macro INV_TXFM_16X16_FN 2-3 0 ; type1, type2, eob_offset
    INV_TXFM_FN          %1, %2, 16x16, %3
%ifidn %1_%2, dct_dct
    movd                xm0, [o(pw_11585x2)]
    pmulhrsw            xm3, xm0, [cq]
    pxor                 m2, m2
    pmulhrsw            xm3, xm0
    pmulhrsw            xm3, [o(pw_512)]
    movd               [cq], xm2
    add                 r3d, 7
    vpbroadcastw         m3, xm3
.dconly_loop:
    mova                xm1, [dstq+strideq*0]
    vinserti128          m1, [dstq+strideq*1], 1
    punpcklbw            m0, m1, m2
    punpckhbw            m1, m2
    paddw                m0, m3
    paddw                m1, m3
    packuswb             m0, m1
    mova         [dstq+strideq*0], xm0
    vextracti128 [dstq+strideq*1], m0, 1
    lea                dstq, [dstq+strideq*2]
    dec                 r3d
    jg .dconly_loop
    RET
%endif
%endmacro

%macro IDCT16_MAIN 2 ; name, idct32 (uses 32-bit intermediates for 11585 multiplies)
%1_fast:
    vpbroadcastd        m10, [o(pw_m9102x2)]
    vpbroadcastd         m8, [o(pw_13623x2)]
    vpbroadcastd        m14, [o(pw_16069x2)]
    vpbroadcastd        m15, [o(pw_3196x2)]
    pmulhrsw            m10, m6
    vpbroadcastd        m12, [o(pw_15137x2)]
    pmulhrsw             m6, m8
    vpbroadcastd         m8, [o(pw_6270x2)]
    pmulhrsw            m14, m2
    vpbroadcastd         m9, [o(pw_11585x2)]
    pmulhrsw             m2, m15
    vpbroadcastd        m15, [o(pd_8192)]
    pmulhrsw            m12, m4
    pmulhrsw             m4, m8
    pmulhrsw             m0, m9
    mova                 m8, m0
    jmp %%main2
ALIGN function_align
%1:
    mova [rsp+gprsize+32*1], m13
    mova [rsp+gprsize+32*2], m9
    vpbroadcastd        m15, [o(pd_8192)]
    ITX_MULSUB_2W        10,  6, 9, 13, 15, 13623,  9102 ; t5a, t6a
    ITX_MULSUB_2W         2, 14, 9, 13, 15,  3196, 16069 ; t4a, t7a
    ITX_MULSUB_2W         4, 12, 9, 13, 15,  6270, 15137 ; t2,  t3
    ITX_MULSUB_2W         0,  8, 9, 13, 15, 11585, 11585 ; t1,  t0
%%main2:
    paddw               m13, m14, m6  ; t7
    psubw               m14, m6       ; t6a
    paddw                m6, m2, m10  ; t4
    psubw                m2, m10      ; t5a
%if %2
    ITX_MULSUB_2W        14, 2, 9, 10, 15, 11585, 11585 ; t5, t6
    psubw               m10, m0, m4   ; t2
    paddw                m4, m0       ; t1
    paddw                m0, m8, m12  ; t0
    psubw                m8, m12      ; t3
    psubw                m9, m4, m2   ; t6
    paddw                m2, m4       ; t1
    psubw                m4, m8, m6   ; t4
    paddw                m8, m6       ; t3
    psubw                m6, m10, m14 ; t5
    paddw               m10, m14      ; t2
%else
    vpbroadcastd         m9, [o(pw_11585x2)]
    psubw               m10, m14, m2
    paddw                m2, m14
    pmulhrsw            m10, m9       ; t5
    pmulhrsw             m2, m9       ; t6
    psubw               m14, m0, m4   ; t2
    paddw                m4, m0       ; t1
    paddw                m0, m8, m12  ; t0
    psubw                m8, m12      ; t3
    psubw                m9, m4, m2   ; t6
    paddw                m2, m4       ; t1
    psubw                m4, m8, m6   ; t4
    paddw                m8, m6       ; t3
    psubw                m6, m14, m10 ; t5
    paddw               m10, m14      ; t2
%endif
    psubw               m14, m0, m13  ; t7
    paddw                m0, m13      ; t0
    test               eobd, eobd
    jl %%main3_fast
    mova                m12, [rsp+gprsize+32*2] ; in9
    mova [rsp+gprsize+32*2], m14
    mova                m13, [rsp+gprsize+32*1] ; in13
    mova [rsp+gprsize+32*1], m2
    mova                m14, [rsp+gprsize+32*0] ; in15
    mova [rsp+gprsize+32*0], m8
    ITX_MULSUB_2W         1, 14, 2, 8, 15,  1606, 16305 ; t8a,  t15a
    ITX_MULSUB_2W        12,  7, 2, 8, 15, 12665, 10394 ; t9a,  t14a
    ITX_MULSUB_2W         5, 11, 2, 8, 15,  7723, 14449 ; t10a, t13a
    ITX_MULSUB_2W        13,  3, 2, 8, 15, 15679,  4756 ; t11a, t12a
    jmp %%main3
%%main3_fast:
    mova [rsp+gprsize+32*2], m14
    mova [rsp+gprsize+32*1], m2
    mova [rsp+gprsize+32*0], m8
    vpbroadcastd        m14, [o(pw_16305x2)]
    vpbroadcastd         m2, [o(pw_1606x2)]
    vpbroadcastd        m12, [o(pw_m10394x2)]
    vpbroadcastd         m8, [o(pw_12665x2)]
    pmulhrsw            m14, m1
    vpbroadcastd        m11, [o(pw_14449x2)]
    pmulhrsw             m1, m2
    vpbroadcastd         m2, [o(pw_7723x2)]
    pmulhrsw            m12, m7
    vpbroadcastd        m13, [o(pw_m4756x2)]
    pmulhrsw             m7, m8
    vpbroadcastd         m8, [o(pw_15679x2)]
    pmulhrsw            m11, m5
    pmulhrsw             m5, m2
    pmulhrsw            m13, m3
    pmulhrsw             m3, m8
%%main3:
    paddw                m2, m11, m3  ; t12
    psubw                m3, m11      ; t13
    psubw               m11, m14, m7  ; t14
    paddw               m14, m7       ; t15
    psubw                m7, m13, m5  ; t10
    paddw                m5, m13      ; t11
    psubw               m13, m1, m12  ; t9
    paddw               m12, m1       ; t8
    ITX_MULSUB_2W        11, 13, 1, 8, 15,   6270, 15137 ; t9a,  t14a
    ITX_MULSUB_2W         3,  7, 1, 8, 15, m15137,  6270 ; t10a, t13a
%if %2
    psubw                m1, m12, m5  ; t11a
    paddw               m12, m5       ; t8a
    psubw                m5, m13, m7  ; t13
    paddw               m13, m7       ; t14
    psubw                m7, m14, m2  ; t12a
    paddw               m14, m2       ; t15a
    psubw                m2, m11, m3  ; t10
    paddw                m3, m11      ; t9
    ITX_MULSUB_2W         5,  2, 8, 11, 15, 11585, 11585 ; t10a, t13a
    ITX_MULSUB_2W         7,  1, 8, 11, 15, 11585, 11585 ; t11,  t12
%else
    vpbroadcastd        m15, [o(pw_11585x2)]
    psubw                m8, m12, m5  ; t11a
    paddw               m12, m5       ; t8a
    psubw                m5, m13, m7  ; t13
    paddw               m13, m7       ; t14
    psubw                m7, m14, m2  ; t12a
    paddw               m14, m2       ; t15a
    psubw                m1, m11, m3  ; t10
    paddw                m3, m11      ; t9
    paddw                m2, m5, m1   ; t13a
    psubw                m5, m1       ; t10a
    paddw                m1, m7, m8   ; t12
    psubw                m7, m8       ; t11
    REPX  {pmulhrsw x, m15}, m2, m5, m1, m7
%endif
    mova                 m8, [rsp+gprsize+32*1] ; t1
    psubw               m15, m0, m14  ; out15
    paddw                m0, m14      ; out0
    psubw               m14, m8, m13  ; out14
    paddw                m8, m13      ; out1
    psubw               m13, m10, m2  ; out13
    paddw                m2, m10      ; out2
    psubw               m11, m4, m7   ; out11
    paddw                m4, m7       ; out4
    mova                 m7, [rsp+gprsize+32*2] ; t7
    psubw               m10, m6, m5   ; out10
    paddw                m5, m6       ; out5
    paddw                m6, m9, m3   ; out6
    psubw                m9, m3       ; out9
    mova                 m3, [rsp+gprsize+32*0] ; t3
    mova [rsp+gprsize+32*1], m8
    psubw                m8, m7, m12  ; out8
    paddw                m7, m12      ; out7
    psubw               m12, m3, m1   ; out12
    paddw                m3, m1       ; out3
%endmacro

INV_TXFM_16X16_FN dct, dct
INV_TXFM_16X16_FN dct, adst, 39-23

cglobal vp9_idct_16x16_internal, 0, 5, 16, 32*6, dst, stride, c, eob, tx2
    mova                 m0, [cq+32*0]
    mova                 m1, [cq+32*1]
    mova                 m2, [cq+32*2]
    mova                 m3, [cq+32*3]
    mova                 m4, [cq+32*4]
    mova                 m5, [cq+32*5]
    mova                 m6, [cq+32*6]
    mova                 m7, [cq+32*7]
    sub                eobd, 39
    jl .pass1_fast
    add                  cq, 32*12
    mova                 m8, [cq-32*4]
    mova                 m9, [cq-32*3]
    mova                m10, [cq-32*2]
    mova                m11, [cq-32*1]
    mova                m12, [cq+32*0]
    mova                m13, [cq+32*1]
    mova                m14, [cq+32*2]
    mova                m15, [cq+32*3]
    mova              [rsp], m15
    call .main
    vextracti128 [rsp+16*4], m0, 1
    mova         [rsp+16*0], xm0
.pass1_end:
    vextracti128 [rsp+16*5], m8, 1
    mova         [rsp+16*1], xm8
    mova                xm1, [rsp+32*1+16*0]
    vinserti128          m8, m9, [rsp+32*1+16*1], 0
    vinserti128          m1, xm9, 1
    vperm2i128           m9, m2, m10, 0x31
    vinserti128          m2, xm10, 1
    vperm2i128          m10, m3, m11, 0x31
    vinserti128          m3, xm11, 1
    vperm2i128          m11, m4, m12, 0x31
    vinserti128          m4, xm12, 1
    vperm2i128          m12, m5, m13, 0x31
    vinserti128          m5, xm13, 1
    vperm2i128          m13, m6, m14, 0x31
    vinserti128          m6, xm14, 1
    vperm2i128          m14, m7, m15, 0x31
    vinserti128          m7, xm15, 1
    mova                m15, [rsp+32*2]
    pxor                 m0, m0
    mov                  r3, -32*12
.zero_loop:
    mova       [cq+r3+32*0], m0
    mova       [cq+r3+32*1], m0
    mova       [cq+r3+32*2], m0
    mova       [cq+r3+32*3], m0
    add                  r3, 32*4
    jle .zero_loop
    punpcklwd            m0, m9, m10
    punpckhwd            m9, m10
    punpcklwd           m10, m15, m8
    punpckhwd           m15, m8
    punpckhwd            m8, m11, m12
    punpcklwd           m11, m12
    punpckhwd           m12, m13, m14
    punpcklwd           m13, m14
    punpckhdq           m14, m11, m13
    punpckldq           m11, m13
    punpckldq           m13, m15, m9
    punpckhdq           m15, m9
    punpckldq            m9, m10, m0
    punpckhdq           m10, m0
    punpckhdq            m0, m8, m12
    punpckldq            m8, m12
    punpcklqdq          m12, m13, m8
    punpckhqdq          m13, m8
    punpcklqdq           m8, m9, m11
    punpckhqdq           m9, m11
    punpckhqdq          m11, m10, m14
    punpcklqdq          m10, m14
    punpcklqdq          m14, m15, m0
    punpckhqdq          m15, m0
    mova                 m0, [rsp]
    mova              [rsp], m15
    punpckhwd           m15, m4, m5
    punpcklwd            m4, m5
    punpckhwd            m5, m0, m1
    punpcklwd            m0, m1
    punpckhwd            m1, m6, m7
    punpcklwd            m6, m7
    punpckhwd            m7, m2, m3
    punpcklwd            m2, m3
    punpckhdq            m3, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m4, m6
    punpckhdq            m4, m6
    punpckhdq            m6, m5, m7
    punpckldq            m5, m7
    punpckldq            m7, m15, m1
    punpckhdq           m15, m1
    punpckhqdq           m1, m0, m2
    punpcklqdq           m0, m2
    punpcklqdq           m2, m3, m4
    punpckhqdq           m3, m4
    punpcklqdq           m4, m5, m7
    punpckhqdq           m5, m7
    punpckhqdq           m7, m6, m15
    punpcklqdq           m6, m15
    jmp                tx2q
.pass1_fast:
    call .main_fast
    mova                xm1, [rsp+32*1]
.pass1_fast_end:
    vinserti128          m0, xm8, 1
    vinserti128          m1, xm9, 1
    vinserti128          m2, xm10, 1
    vinserti128          m3, xm11, 1
    vinserti128          m4, xm12, 1
    vinserti128          m5, xm13, 1
    vinserti128          m6, xm14, 1
    vinserti128          m7, xm15, 1
    pxor                 m8, m8
    REPX {mova [cq+32*x], m8}, 0, 1, 2, 3, 4, 5, 6, 7
    call .transpose_8x8
    jmp                tx2q
.pass2:
    test               eobd, eobd
    jl .pass2_fast
    call .main
    jmp .pass2_end
.pass2_fast:
    call .main_fast
.pass2_end:
    vpbroadcastd         m1, [o(pw_512)]
    REPX   {pmulhrsw x, m1}, m0, m2, m4, m5, m6, m7, m8, m9, m10, m11, m12, m14
.end:
    REPX   {pmulhrsw x, m1}, m3, m13, m15
    pmulhrsw             m1, [rsp+32*1]
    mova              [rsp], m6
    lea                  r3, [strideq*3]
    WRITE_16X2            0,  1,  6,  0, strideq*0, strideq*1
    WRITE_16X2            2,  3,  0,  1, strideq*2, r3
    lea                dstq, [dstq+strideq*4]
    WRITE_16X2            4,  5,  0,  1, strideq*0, strideq*1
    WRITE_16X2        [rsp],  7,  0,  1, strideq*2, r3
    lea                dstq, [dstq+strideq*4]
    WRITE_16X2            8,  9,  0,  1, strideq*0, strideq*1
    WRITE_16X2           10, 11,  0,  1, strideq*2, r3
    lea                dstq, [dstq+strideq*4]
    WRITE_16X2           12, 13,  0,  1, strideq*0, strideq*1
    WRITE_16X2           14, 15,  0,  1, strideq*2, r3
    RET
ALIGN function_align
    IDCT16_MAIN       .main, 0
    ret
ALIGN function_align
.transpose_8x8:
    punpckhwd            m8, m4, m5
    punpcklwd            m4, m5
    punpckhwd            m5, m0, m1
    punpcklwd            m0, m1
    punpckhwd            m1, m6, m7
    punpcklwd            m6, m7
    punpckhwd            m7, m2, m3
    punpcklwd            m2, m3
    punpckhdq            m3, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m4, m6
    punpckhdq            m4, m6
    punpckhdq            m6, m5, m7
    punpckldq            m5, m7
    punpckldq            m7, m8, m1
    punpckhdq            m8, m1
    punpckhqdq           m1, m0, m2
    punpcklqdq           m0, m2
    punpcklqdq           m2, m3, m4
    punpckhqdq           m3, m4
    punpcklqdq           m4, m5, m7
    punpckhqdq           m5, m7
    punpckhqdq           m7, m6, m8
    punpcklqdq           m6, m8
    ret

%macro ADST_MULSUB_4W_FAST 12 ; dst/src[1-4], tmp[1-3], rnd, coef[1-4]
    vpbroadcastd        m%5, [o(pw_%11_m%10)]
    vpbroadcastd        m%6, [o(pw_m%11_m%10)]
    punpckhwd           m%7, m%3, m%2
    punpcklwd           m%3, m%2
    pmaddwd             m%2, m%3, m%5
    pmaddwd             m%5, m%7
    pmaddwd             m%4, m%3, m%6
    pmaddwd             m%6, m%7
    REPX     {paddd x, m%8}, m%2, m%5, m%4, m%6
    REPX     {psrad x, 14 }, m%2, m%5, m%4, m%6
    packssdw            m%2, m%5
    vpbroadcastd        m%5, [o(pw_%12_%9)]
    packssdw            m%4, m%6
    vpbroadcastd        m%6, [o(pw_m%12_%9)]
    pmaddwd             m%1, m%3, m%5
    pmaddwd             m%5, m%7
    pmaddwd             m%3, m%6
    pmaddwd             m%6, m%7
    REPX     {paddd x, m%8}, m%1, m%5, m%3, m%6
    REPX     {psrad x, 14 }, m%1, m%5, m%3, m%6
    packssdw            m%1, m%5
    packssdw            m%3, m%6
%endmacro

INV_TXFM_16X16_FN adst, dct, 39-18
INV_TXFM_16X16_FN adst, adst

cglobal vp9_iadst_16x16_internal, 0, 5, 16, 32*6, dst, stride, c, eob, tx2
    mova                 m0, [cq+32*0]
    mova                 m1, [cq+32*1]
    mova                 m2, [cq+32*2]
    mova                 m3, [cq+32*3]
    mova                 m4, [cq+32*4]
    mova                 m5, [cq+32*5]
    mova                 m6, [cq+32*6]
    mova                 m7, [cq+32*7]
    sub                eobd, 39
    jl .pass1_fast
    add                  cq, 32*12
    mova                 m8, [cq-32*4]
    mova                 m9, [cq-32*3]
    mova                m10, [cq-32*2]
    mova                m11, [cq-32*1]
    mova                m12, [cq+32*0]
    mova                m13, [cq+32*1]
    mova                m14, [cq+32*2]
    mova                m15, [cq+32*3]
    mova         [rsp+32*0], m15
    call .main
    call .pass1_main_part2
    mova        [rsp+32*1], m1
    jmp m(vp9_idct_16x16_internal).pass1_end
.pass1_fast:
    call .main_fast
    call .pass1_main_part2
    mova                xm0, [rsp+32*0]
    jmp m(vp9_idct_16x16_internal).pass1_fast_end
.pass2:
    test               eobd, eobd
    jl .pass2_fast
    call .main
    jmp .pass2_end
.pass2_fast:
    call .main_fast
.pass2_end:
    ; In pass 2 we're going to clip to pixels afterwards anyway, so clipping to
    ; 16-bit here will produce the same result as using 32-bit intermediates.
    paddsw               m5, m10, m11 ; -out5
    psubsw              m10, m11      ;  out10
    psubsw              m11, m8, m4   ;  out11
    paddsw               m4, m8       ;  out4
    psubsw               m8, m7, m9   ;  out8
    paddsw               m7, m9       ; -out7
    psubsw               m9, m6, m1   ;  out9
    paddsw               m6, m1       ;  out6
    vpbroadcastd         m1, [o(pw_11585x2)]
    REPX   {pmulhrsw x, m1}, m4, m6, m8, m9, m10, m11
    vpbroadcastd         m1, [o(pw_m11585x2)]
    pmulhrsw             m5, m1
    pmulhrsw             m7, m1
    vpbroadcastd         m1, [o(pw_512)]
    REPX   {pmulhrsw x, m1}, m0, m2, m4, m5, m6, m7, m8, m9, m10, m11, m12, m14
    vpbroadcastd         m1, [o(pw_m512)]
    jmp m(vp9_idct_16x16_internal).end
ALIGN function_align
.main_fast:
    mova [rsp+gprsize+32*1], m0
    mova [rsp+gprsize+32*2], m3
    mova [rsp+gprsize+32*3], m4
    vpbroadcastd        m15, [o(pd_8192)]
    ADST_MULSUB_4W_FAST  13, 2, 5, 10, 0, 3, 4, 15, 3981, 15893, 14053, 8423
    ADST_MULSUB_4W_FAST   9, 6, 1, 14, 0, 3, 4, 15, 9760, 13160, 16207, 2404
    jmp .main2
ALIGN function_align
.main:
    mova [rsp+gprsize+32*1], m0
    mova [rsp+gprsize+32*2], m3
    mova [rsp+gprsize+32*3], m4
    mova [rsp+gprsize+32*4], m12
    mova [rsp+gprsize+32*5], m8
    vpbroadcastd        m15, [o(pd_8192)]
    ADST_MULSUB_4W       13,  2,  5, 10,  0,  3,  4,  8, 12, 15,  3981, 15893, 14053,  8423 ; t2a,  t3a, t10a, t11a
    ADST_MULSUB_4W        9,  6,  1, 14,  0,  3,  4,  8, 12, 15,  9760, 13160, 16207,  2404 ; t6a,  t7a, t14a, t15a
.main2:
    ADST_MULSUB_4W        5, 10, 14,  1,  0,  3,  4,  8, 12, 15, 13623,  9102               ; t10a, t11a, t14a, t15a
    psubw                m4, m2, m6  ; t7
    paddw                m2, m6      ; t3
    psubw                m8, m13, m9 ; t6
    paddw               m13, m9      ; t2
    mova                 m0, [rsp+gprsize+32*0] ; in15
    mova [rsp+gprsize+32*0], m10
    mova                m10, [rsp+gprsize+32*1] ; in0
    mova [rsp+gprsize+32*1], m13
    mova                m13, [rsp+gprsize+32*2] ; in3
    mova [rsp+gprsize+32*2], m1
    mova                 m6, [rsp+gprsize+32*3] ; in4
    mova [rsp+gprsize+32*3], m2
    mova                 m2, [rsp+gprsize+32*4] ; in12
    mova [rsp+gprsize+32*4], m5
    mova                 m5, [rsp+gprsize+32*5] ; in8
    mova [rsp+gprsize+32*5], m14
    test               eobd, eobd
    jl .main3_fast
    ADST_MULSUB_4W        0, 10,  7,  5,  1,  3,  9, 12, 14, 15,   804, 16364, 12140, 11003 ; t0a,  t1a,  t8a,  t9a
    ADST_MULSUB_4W       11,  6, 13,  2,  1,  3,  9, 12, 14, 15,  7005, 14811, 15426,  5520 ; t4a,  t5a,  t12a, t13a
    jmp .main3
.main3_fast:
    ADST_MULSUB_4W_FAST   0, 10,  7,  5,  1,  3,  9, 15,   804, 16364, 12140, 11003
    ADST_MULSUB_4W_FAST  11,  6, 13,  2,  1,  3,  9, 15,  7005, 14811, 15426,  5520
.main3:
    ADST_MULSUB_4W        7,  5,  2, 13,  1,  3,  9, 12, 14, 15,  3196, 16069               ; t8a,  t9a,  t12a, t13a
    psubw                m3, m0, m11 ; t4
    paddw                m0, m11     ; t0
    psubw               m12, m10, m6 ; t5
    paddw               m10, m6      ; t1
    mova                m11, [rsp+gprsize+32*5] ; t14a
    mova [rsp+gprsize+32*5], m10
    mova                m10, [rsp+gprsize+32*2] ; t15a
    mova [rsp+gprsize+32*2], m0
    ADST_MULSUB_4W        2, 13, 10, 11,  0,  1,  6,  9, 14, 15,  6270, 15137 ;  out2, -out13, t14a, t15a
    ADST_MULSUB_4W        3, 12,  4,  8,  0,  1,  6,  9, 14, 15,  6270, 15137 ; -out3,  out12, t6,   t7
    mova                 m6, [rsp+gprsize+32*4] ; t10a
    mova                m14, [rsp+gprsize+32*0] ; t11a
    mova                 m9, [rsp+gprsize+32*1] ; t2
    mova                 m0, [rsp+gprsize+32*2] ; t0
    mova                m15, [rsp+gprsize+32*5] ; t1
    psubw                m1, m7, m6  ;  t10
    paddw                m7, m6      ; -out1
    psubw                m6, m5, m14 ;  t11
    paddw               m14, m5      ;  out14
    mova                 m5, [rsp+gprsize+32*3] ; t3
    mova [rsp+gprsize+32*1], m7
    psubw                m7, m0, m9  ;  t2a
    paddw                m0, m9      ;  out0
    psubw                m9, m15, m5 ;  t3a
    paddw               m15, m5      ; -out15
    ret
ALIGN function_align
.pass1_main_part2:
    mova         [rsp+gprsize+16*0], xm0
    vextracti128 [rsp+gprsize+16*4], m0, 1
    mova         [rsp+gprsize+32*3], m15
    mova         [rsp+gprsize+32*4], m13
    mova         [rsp+gprsize+32*5], m3
    vpbroadcastd        m15, [o(pw_m11585_11585)]
    vpbroadcastd        m13, [o(pw_11585_11585)]
    vpbroadcastd         m3, [o(pd_8192)]
    punpcklwd            m5, m11, m10
    punpckhwd           m11, m10
    pmaddwd             m10, m15, m5
    pmaddwd              m0, m15, m11
    pmaddwd              m5, m13
    pmaddwd             m11, m13
    paddd               m10, m3
    paddd                m0, m3
    psubd                m5, m3, m5
    psubd               m11, m3, m11
    REPX      {psrad x, 14}, m10, m0, m5, m11
    packssdw            m10, m0  ; out10
    packssdw             m5, m11 ; out5
    punpcklwd           m11, m8, m4
    punpckhwd            m8, m4
    pmaddwd              m4, m13, m11
    pmaddwd              m0, m13, m8
    pmaddwd             m11, m15
    pmaddwd              m8, m15
    paddd                m4, m3
    paddd                m0, m3
    psubd               m11, m3, m11
    psubd                m8, m3, m8
    REPX      {psrad x, 14}, m4, m0, m11, m8
    packssdw             m4, m0  ; out4
    packssdw            m11, m8  ; out11
    punpcklwd            m8, m9, m7
    punpckhwd            m9, m7
    pmaddwd              m7, m13, m8
    pmaddwd              m0, m13, m9
    pmaddwd              m8, m15
    pmaddwd              m9, m15
    psubd                m7, m3, m7
    psubd                m0, m3, m0
    paddd                m8, m3
    paddd                m9, m3
    REPX      {psrad x, 14}, m7, m0, m8, m9
    packssdw             m7, m0  ; out7
    packssdw             m8, m9  ; out8
    punpckhwd            m0, m6, m1
    punpcklwd            m6, m1
    pmaddwd              m1, m15, m0
    pmaddwd              m9, m15, m6
    pmaddwd              m0, m13
    pmaddwd              m6, m13
    psubd                m1, m3, m1
    psubd                m9, m3, m9
    paddd                m0, m3
    paddd                m6, m3
    pxor                 m3, m3
    psubw               m15, m3, [rsp+gprsize+32*3] ; out15
    REPX      {psrad x, 14}, m1, m9, m0, m6
    psubw               m13, m3, [rsp+gprsize+32*4] ; out13
    packssdw             m9, m1  ; out7
    psubw                m1, m3, [rsp+gprsize+32*1] ; out1
    packssdw             m6, m0  ; out8
    psubw                m3, [rsp+gprsize+32*5]     ; out3
    ret

%macro LOAD_8ROWS 2 ; src, stride
    mova                 m0, [%1+%2*0]
    mova                 m1, [%1+%2*1]
    mova                 m2, [%1+%2*2]
    mova                 m3, [%1+%2*3]
    mova                 m4, [%1+%2*4]
    mova                 m5, [%1+%2*5]
    mova                 m6, [%1+%2*6]
    mova                 m7, [%1+%2*7]
%endmacro

%macro LOAD_8ROWS_H 2 ; src, stride
    mova                 m8, [%1+%2*0]
    mova                 m9, [%1+%2*1]
    mova                m10, [%1+%2*2]
    mova                m11, [%1+%2*3]
    mova                m12, [%1+%2*4]
    mova                m13, [%1+%2*5]
    mova                m14, [%1+%2*6]
    mova                m15, [%1+%2*7]
%endmacro

; Perform the final sumsub step and YMM lane shuffling
%macro IDCT32_PASS1_END 4 ; row[1-2], tmp[1-2]
    mova                m%3, [tmp2q+32*( 3-%1)]
    psubw               m%4, m%1, m%3
    paddw               m%1, m%3
    mova                m%3, [tmp1q+32*(11-%2)]
    mova         [tmp1q+32*(11-%2)+16], xm%4
    vextracti128 [tmp2q+32*( 3-%1)+16], m%4, 1
    paddw               m%4, m%2, m%3
    psubw               m%2, m%3
    mova         [tmp1q+32*(11-%2)], xm%2
    vextracti128 [tmp2q+32*( 3-%1)], m%2, 1
    vperm2i128          m%2, m%1, m%4, 0x31
    vinserti128         m%1, xm%4, 1
%endmacro

%macro IDCT32_PASS2_END 7 ; coefs[1-2], tmp[1-2], rnd, offset[1-2]
    mova                m%4, [%2]
    paddw               m%3, m%1, m%4
    psubw               m%1, m%4
    pmovzxbw            m%4, [dstq+%6]
    pmulhrsw            m%3, m%5
    pmulhrsw            m%1, m%5
    paddw               m%3, m%4
    pmovzxbw            m%4, [r2+%7]
    paddw               m%1, m%4
    packuswb            m%3, m%1
    vpermq              m%3, m%3, q3120
    mova          [dstq+%6], xm%3
    vextracti128    [r2+%7], m%3, 1
%endmacro

cglobal vp9_idct_idct_32x32_add, 4, 4, 0, dst, stride, c, eob
    lea                  r6, [o_base]
    sub                eobd, 1
    jnz .pass1
    movd                xm0, [o(pw_11585x2)]
    pmulhrsw            xm5, xm0, [cq]
    pxor                 m4, m4
    pmulhrsw            xm5, xm0
    pmulhrsw            xm5, [o(pw_512)]
    movd               [cq], xm4
    or                  r3d, 16
    vpbroadcastw         m5, xm5
.dconly_loop:
    mova                 m2, [dstq+strideq*0]
    mova                 m3, [dstq+strideq*1]
    punpcklbw            m0, m2, m4
    punpckhbw            m2, m4
    punpcklbw            m1, m3, m4
    punpckhbw            m3, m4
    REPX      {paddw x, m5}, m0, m2, m1, m3
    packuswb             m0, m2
    packuswb             m1, m3
    mova   [dstq+strideq*0], m0
    mova   [dstq+strideq*1], m1
    lea                dstq, [dstq+strideq*2]
    dec                 r3d
    jg .dconly_loop
    RET
.pass1:
    PROLOGUE              0, 9, 16, 32*67, dst, stride, c, eob, tmp1, \
                                           tmp2, base, tmp3, tmp4
    %undef cmp
    lea               tmp1q, [rsp+32*7]
    sub                eobd, 135
    lea               tmp2q, [tmp1q+32*8]
    mov               tmp4d, eobd
.pass1_loop:
    LOAD_8ROWS      cq+64*1, 64*2
    test               eobd, eobd
    jl .pass1_fast
    LOAD_8ROWS_H   cq+64*17, 64*2
    call .main
    LOAD_8ROWS_H   cq+64*16, 64*2
    mova              [rsp], m15
    LOAD_8ROWS      cq+64*0, 64*2
    call .idct16
    mov               tmp3d, 64*30
    jmp .pass1_loop_end
.pass1_fast:
    call .main_fast
    LOAD_8ROWS      cq+64*0, 64*2
    call .idct16_fast
    mov               tmp3d, 64*14
.pass1_loop_end:
    pxor                 m1, m1
.zero_loop:
    mova    [cq+tmp3q+64*1], m1
    mova    [cq+tmp3q+64*0], m1
    mova    [cq+tmp3q-64*1], m1
    mova    [cq+tmp3q-64*2], m1
    sub               tmp3d, 64*4
    jg .zero_loop
    mova         [rsp+32*0], m9
    IDCT32_PASS1_END      0,  8,  1,  9
    IDCT32_PASS1_END      2, 10,  1,  9
    IDCT32_PASS1_END      3, 11,  1,  9
    IDCT32_PASS1_END      4, 12,  1,  9
    IDCT32_PASS1_END      5, 13,  1,  9
    IDCT32_PASS1_END      6, 14,  1,  9
    IDCT32_PASS1_END      7, 15,  1,  9
    mova                 m1, [rsp+32*1]
    mova                 m9, [rsp+32*0]
    mova         [rsp+32*0], m6
    mova         [rsp+32*1], m7
    IDCT32_PASS1_END      1,  9,  6,  7
    mova                 m7, [rsp+32*1]
    punpckhwd            m6, m12, m13
    punpcklwd           m12, m13
    punpckhwd           m13, m8, m9
    punpcklwd            m8, m9
    punpckhwd            m9, m14, m15
    punpcklwd           m14, m15
    punpckhwd           m15, m10, m11
    punpcklwd           m10, m11
    punpckhdq           m11, m8, m10
    punpckldq            m8, m10
    punpckldq           m10, m12, m14
    punpckhdq           m12, m14
    punpckhdq           m14, m13, m15
    punpckldq           m13, m15
    punpckldq           m15, m6, m9
    punpckhdq            m6, m9
    punpckhqdq           m9, m8, m10
    punpcklqdq           m8, m10
    punpcklqdq          m10, m11, m12
    punpckhqdq          m11, m12
    punpcklqdq          m12, m13, m15
    punpckhqdq          m13, m15
    punpckhqdq          m15, m14, m6
    punpcklqdq          m14, m6
    mova                 m6, [rsp+32*0]
    mova         [rsp+32*0], m8
    call m(vp9_idct_16x16_internal).transpose_8x8
    lea               tmp3q, [tmp1q+32*32]
    mova                 m8, [rsp]
    mova       [tmp3q-32*4], m0
    mova       [tmp3q-32*3], m2
    mova       [tmp3q-32*2], m4
    mova       [tmp3q-32*1], m6
    mova       [tmp3q+32*0], m8
    mova       [tmp3q+32*1], m10
    mova       [tmp3q+32*2], m12
    mova       [tmp3q+32*3], m14
    add               tmp3q, 32*8
    mova       [tmp3q-32*4], m1
    mova       [tmp3q-32*3], m3
    mova       [tmp3q-32*2], m5
    mova       [tmp3q-32*1], m7
    mova       [tmp3q+32*0], m9
    mova       [tmp3q+32*1], m11
    mova       [tmp3q+32*2], m13
    mova       [tmp3q+32*3], m15
    mova                 m0, [tmp1q-32*4]
    mova                 m1, [tmp1q-32*3]
    mova                 m2, [tmp1q-32*2]
    mova                 m3, [tmp1q-32*1]
    mova                 m4, [tmp1q+32*0]
    mova                 m5, [tmp1q+32*1]
    mova                 m6, [tmp1q+32*2]
    mova                 m7, [tmp1q+32*3]
    call m(vp9_idct_16x16_internal).transpose_8x8
    mova       [tmp1q-32*4], m0
    mova                 m0, [tmp2q-32*4]
    mova       [tmp2q-32*4], m1
    mova                 m1, [tmp2q-32*3]
    mova       [tmp1q-32*3], m2
    mova                 m2, [tmp2q-32*2]
    mova       [tmp2q-32*3], m3
    mova                 m3, [tmp2q-32*1]
    mova       [tmp1q-32*2], m4
    mova                 m4, [tmp2q+32*0]
    mova       [tmp2q-32*2], m5
    mova                 m5, [tmp2q+32*1]
    mova       [tmp1q-32*1], m6
    mova                 m6, [tmp2q+32*2]
    mova       [tmp2q-32*1], m7
    mova                 m7, [tmp2q+32*3]
    call m(vp9_idct_16x16_internal).transpose_8x8
    mova       [tmp1q+32*0], m0
    mova       [tmp2q+32*0], m1
    mova       [tmp1q+32*1], m2
    mova       [tmp2q+32*1], m3
    mova       [tmp1q+32*2], m4
    mova       [tmp2q+32*2], m5
    mova       [tmp1q+32*3], m6
    mova       [tmp2q+32*3], m7
    add                  cq, 32
    add               tmp1q, 32*16
    add               tmp2q, 32*16
    add               tmp4d, 0x80000000
    jnc .pass1_loop
    add               tmp1q, 32*24
    imul                 r2, strideq, 19
    lea               tmp4q, [strideq*3]
    add                  r2, dstq
    test               eobd, eobd
    jge .pass2_loop
    add               tmp1q, 32*16
    add               tmp2q, 32*16
    add               tmp3q, 32*16
.pass2_loop:
    LOAD_8ROWS   tmp2q-32*4, 32
    test               eobd, eobd
    jl .pass2_fast
    LOAD_8ROWS_H tmp3q-32*4, 32
    call .main
    sub               tmp3q, 32*8
    LOAD_8ROWS_H tmp3q-32*4, 32
    sub               tmp3q, 32*16
    LOAD_8ROWS   tmp3q-32*4, 32
    mova              [rsp], m15
    call .idct16
    jmp .pass2_loop_end
.pass2_fast:
    call .main_fast
    sub               tmp3q, 32*24
    LOAD_8ROWS   tmp3q-32*4, 32
    call .idct16_fast
.pass2_loop_end:
    mova         [rsp+32*0], m7
    mova         [rsp+32*2], m15
    vpbroadcastd        m15, [o(pw_512)]
    IDCT32_PASS2_END      0, tmp2q+32*3, 1, 7, 15, strideq*0, tmp4q*4
    IDCT32_PASS2_END      4, tmp2q-32*1, 0, 7, 15, strideq*4, strideq*8
    IDCT32_PASS2_END      8, tmp1q+32*3, 0, 4, 15, strideq*8, strideq*4
    IDCT32_PASS2_END     12, tmp1q-32*1, 0, 4, 15, tmp4q*4,   strideq*0
    add                dstq, strideq
    sub                  r2, strideq
    mova                 m1, [rsp+32*1]
    IDCT32_PASS2_END      1, tmp2q+32*2, 0, 4, 15, strideq*0, tmp4q*4
    IDCT32_PASS2_END      5, tmp2q-32*2, 0, 4, 15, strideq*4, strideq*8
    IDCT32_PASS2_END      9, tmp1q+32*2, 0, 4, 15, strideq*8, strideq*4
    IDCT32_PASS2_END     13, tmp1q-32*2, 0, 4, 15, tmp4q*4,   strideq*0
    add                dstq, strideq
    sub                  r2, strideq
    IDCT32_PASS2_END      2, tmp2q+32*1, 0, 4, 15, strideq*0, tmp4q*4
    IDCT32_PASS2_END      6, tmp2q-32*3, 0, 4, 15, strideq*4, strideq*8
    IDCT32_PASS2_END     10, tmp1q+32*1, 0, 4, 15, strideq*8, strideq*4
    IDCT32_PASS2_END     14, tmp1q-32*3, 0, 4, 15, tmp4q*4,   strideq*0
    add                dstq, strideq
    sub                  r2, strideq
    mova                 m7, [rsp+32*0]
    mova                 m1, [rsp+32*2]
    IDCT32_PASS2_END      3, tmp2q+32*0, 0, 4, 15, strideq*0, tmp4q*4
    IDCT32_PASS2_END      7, tmp2q-32*4, 0, 4, 15, strideq*4, strideq*8
    IDCT32_PASS2_END     11, tmp1q+32*0, 0, 4, 15, strideq*8, strideq*4
    IDCT32_PASS2_END      1, tmp1q-32*4, 0, 4, 15, tmp4q*4,   strideq*0
    lea               tmp3q, [tmp1q-32*32]
    cmp               tmp2q, tmp3q
    jb .ret
    sub               tmp2q, 32*32
    sub                dstq, tmp4q
    lea                  r2, [r2+tmp4q+16]
    add                dstq, 16
    jmp .pass2_loop
.ret:
    RET
ALIGN function_align
    IDCT16_MAIN     .idct16, 1
    ret
ALIGN function_align
.main_fast:
    mova       [tmp1q+32*0], m7
    vpbroadcastd        m11, [o(pw_14811x2)]
    vpbroadcastd         m7, [o(pw_7005x2)]
    vpbroadcastd        m12, [o(pw_m5520x2)]
    vpbroadcastd         m8, [o(pw_15426x2)]
    vpbroadcastd        m13, [o(pw_15893x2)]
    vpbroadcastd        m15, [o(pw_3981x2)]
    pmulhrsw            m11, m4  ; t29a
    vpbroadcastd        m10, [o(pw_m8423x2)]
    pmulhrsw             m4, m7  ; t18a
    vpbroadcastd         m7, [o(pw_14053x2)]
    pmulhrsw            m12, m3  ; t19a
    vpbroadcastd         m9, [o(pw_13160x2)]
    pmulhrsw             m3, m8  ; t28a
    vpbroadcastd         m8, [o(pw_9760x2)]
    pmulhrsw            m13, m2  ; t27a
    vpbroadcastd        m14, [o(pw_m2404x2)]
    pmulhrsw             m2, m15 ; t20a
    vpbroadcastd        m15, [o(pw_16207x2)]
    pmulhrsw            m10, m5  ; t21a
    pmulhrsw             m5, m7  ; t26a
    pmulhrsw             m9, m6  ; t25a
    pmulhrsw             m6, m8  ; t22a
    pmulhrsw            m14, m1  ; t23a
    pmulhrsw             m1, m15 ; t24a
    vpbroadcastd        m15, [o(pd_8192)]
    jmp .main2
ALIGN function_align
.main:
    mova       [tmp1q+32*0], m7
    mova       [tmp1q-32*1], m15
    mova       [tmp1q-32*2], m8
    vpbroadcastd        m15, [o(pd_8192)]
    ITX_MULSUB_2W         4, 11,  7,  8, 15,  7005, 14811 ; t18a, t29a
    ITX_MULSUB_2W        12,  3,  7,  8, 15, 15426,  5520 ; t19a, t28a
    ITX_MULSUB_2W         2, 13,  7,  8, 15,  3981, 15893 ; t20a, t27a
    ITX_MULSUB_2W        10,  5,  7,  8, 15, 14053,  8423 ; t21a, t26a
    ITX_MULSUB_2W         6,  9,  7,  8, 15,  9760, 13160 ; t22a, t25a
    ITX_MULSUB_2W        14,  1,  7,  8, 15, 16207,  2404 ; t23a, t24a
.main2:
    psubw                m7, m12, m4  ; t18
    paddw               m12, m4       ; t19
    psubw                m4, m2, m10  ; t21
    paddw                m2, m10      ; t20
    psubw               m10, m14, m6  ; t22
    paddw               m14, m6       ; t23
    psubw                m6, m1, m9   ; t25
    paddw                m1, m9       ; t24
    psubw                m9, m13, m5  ; t26
    paddw               m13, m5       ; t27
    psubw                m5, m3, m11  ; t29
    paddw                m3, m11      ; t28
    ITX_MULSUB_2W         5,  7,  8, 11, 15, m16069,  3196 ; t18a, t29a
    ITX_MULSUB_2W         9,  4,  8, 11, 15,  13623,  9102 ; t21a, t26a
    ITX_MULSUB_2W         6, 10,  8, 11, 15,  m9102, 13623 ; t22a, t25a
    psubw                m8, m14, m2  ; t20a
    paddw               m14, m2       ; t23a
    psubw                m2, m1, m13  ; t27a
    paddw                m1, m13      ; t24a
    psubw               m13, m6, m9   ; t21
    paddw                m6, m9       ; t22
    psubw                m9, m10, m4  ; t26
    paddw               m10, m4       ; t25
    ITX_MULSUB_2W         2,  8,  4, 11, 15, m15137,  6270 ; t20,  t27
    ITX_MULSUB_2W         9, 13,  4, 11, 15, m15137,  6270 ; t21a, t26a
    mova       [tmp1q+32*1], m6
    mova       [tmp1q+32*2], m14
    mova       [tmp1q+32*3], m1
    mova                 m4, [tmp1q+32*0] ; in15
    test               eobd, eobd
    jl .main3_fast
    mova                 m6, [tmp1q-32*1] ; in31
    mova                m14, [tmp1q-32*2] ; in17
    ITX_MULSUB_2W         0,  6,  1, 11, 15,   804, 16364 ; t16a, t31a
    ITX_MULSUB_2W        14,  4,  1, 11, 15, 12140, 11003 ; t17a, t30a
    jmp .main3
.main3_fast:
    vpbroadcastd         m6, [o(pw_16364x2)]
    vpbroadcastd         m1, [o(pw_804x2)]
    vpbroadcastd        m14, [o(pw_m11003x2)]
    vpbroadcastd        m11, [o(pw_12140x2)]
    pmulhrsw             m6, m0       ; t31a
    pmulhrsw             m0, m1       ; t16a
    pmulhrsw            m14, m4       ; t17a
    pmulhrsw             m4, m11      ; t30a
.main3:
    psubw                m1, m0, m14  ; t17
    paddw                m0, m14      ; t16
    psubw               m14, m6, m4   ; t30
    paddw                m4, m6       ; t31
    ITX_MULSUB_2W        14,  1,  6, 11, 15,  3196, 16069 ; t17a, t30a
    psubw                m6, m0, m12  ; t19a
    paddw                m0, m12      ; t16a
    psubw               m12, m4, m3   ; t28a
    paddw                m4, m3       ; t31a
    psubw                m3, m14, m5  ; t18
    paddw               m14, m5       ; t17
    psubw                m5, m1, m7   ; t29
    paddw                m1, m7       ; t30
    ITX_MULSUB_2W         5,  3,  7, 11, 15,  6270, 15137 ; t18a, t29a
    ITX_MULSUB_2W        12,  6,  7, 11, 15,  6270, 15137 ; t19,  t28
    psubw                m7, m1, m10  ; t25a
    paddw                m1, m10      ; t30a
    psubw               m10, m5, m9   ; t21
    paddw                m5, m9       ; t18
    psubw                m9, m12, m2  ; t20a
    paddw               m12, m2       ; t19a
    psubw                m2, m3, m13  ; t26
    paddw                m3, m13      ; t29
    psubw               m13, m6, m8   ; t27a
    paddw                m6, m8       ; t28a
    mova       [tmp1q-32*2], m5
    mova       [tmp1q-32*1], m12
    mova       [tmp2q+32*0], m6
    mova       [tmp2q+32*1], m3
    mova       [tmp2q+32*2], m1
    mova                 m5, [tmp1q+32*1] ; t22
    mova                 m6, [tmp1q+32*2] ; t23
    mova                 m3, [tmp1q+32*3] ; t24a
    psubw                m1, m14, m5  ; t22a
    paddw               m14, m5       ; t17a
    psubw                m5, m0, m6   ; t23
    paddw                m0, m6       ; t16
    psubw                m6, m4, m3   ; t24
    paddw                m4, m3       ; t31
    vpbroadcastd         m8, [o(pw_m11585_11585)]
    vpbroadcastd         m3, [o(pw_11585_11585)]
    mova       [tmp1q-32*4], m0
    mova       [tmp1q-32*3], m14
    mova       [tmp2q+32*3], m4
    ITX_MULSUB_2W        13,  9,  0,  4, 15,  3,  8 ; t20,  t27
    ITX_MULSUB_2W         2, 10,  0,  4, 15,  3,  8 ; t21a, t26a
    ITX_MULSUB_2W         7,  1,  0,  4, 15,  3,  8 ; t22,  t25
    ITX_MULSUB_2W         6,  5,  0,  4, 15,  3,  8 ; t23a, t24a
    mova       [tmp1q+32*0], m13
    mova       [tmp1q+32*1], m2
    mova       [tmp1q+32*2], m7
    mova       [tmp1q+32*3], m6
    mova       [tmp2q-32*4], m5
    mova       [tmp2q-32*3], m1
    mova       [tmp2q-32*2], m10
    mova       [tmp2q-32*1], m9
    ret

%endif
