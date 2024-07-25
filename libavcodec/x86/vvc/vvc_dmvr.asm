; /*
; * Provide AVX2 luma dmvr functions for VVC decoding
; * Copyright (c) 2024 Nuo Mi
; *
; * This file is part of FFmpeg.
; *
; * FFmpeg is free software; you can redistribute it and/or
; * modify it under the terms of the GNU Lesser General Public
; * License as published by the Free Software Foundation; either
; * version 2.1 of the License, or (at your option) any later version.
; *
; * FFmpeg is distributed in the hope that it will be useful,
; * but WITHOUT ANY WARRANTY; without even the implied warranty of
; * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; * Lesser General Public License for more details.
; *
; * You should have received a copy of the GNU Lesser General Public
; * License along with FFmpeg; if not, write to the Free Software
; * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
; */
%include "libavutil/x86/x86util.asm"

%define MAX_PB_SIZE             128

SECTION_RODATA 32

shift_12   times 2  dw 1 << (15 - (12 - 10))
shift3_8   times 2  dw 1 << (15 - (8 - 6))
shift3_10  times 2  dw 1 << (15 - (10 - 6))
shift3_12  times 2  dw 1 << (15 - (12 - 6))
pw_16      times 2  dw 16

%if ARCH_X86_64

%if HAVE_AVX2_EXTERNAL

SECTION .text

%define pstride (bd / 10 + 1)

; LOAD(dst, src)
%macro LOAD_W16 2
%if bd == 8
    pmovzxbw               %1, %2
%else
    movu                   %1, %2
%endif
%endmacro

%macro SHIFT_W16 2
%if bd == 8
    psllw                  %1, (10 - bd)
%elif bd == 10
    ; nothing
%else
    pmulhrsw               %1, %2
%endif
%endmacro

%macro SAVE_W16 2
    movu                   %1, %2
%endmacro

; NEXT_4_LINES(is_h)
%macro NEXT_4_LINES 1
    lea                 dstq, [dstq + dsq*4]
    lea                 srcq, [srcq + ssq*4]
%if %1
    lea                src1q, [srcq + pstride]
%endif
%endmacro


; DMVR_4xW16(dst, dst_stride, dst_stride3, src, src_stride, src_stride3)
%macro DMVR_4xW16 6
    LOAD_W16               m0, [%4]
    LOAD_W16               m1, [%4 + %5]
    LOAD_W16               m2, [%4 + 2 * %5]
    LOAD_W16               m3, [%4 + %6]

    SHIFT_W16              m0, m4
    SHIFT_W16              m1, m4
    SHIFT_W16              m2, m4
    SHIFT_W16              m3, m4

    SAVE_W16    [%1]         , m0
    SAVE_W16    [%1 + %2]    , m1
    SAVE_W16    [%1 + 2 * %2], m2
    SAVE_W16    [%1 + %3]    , m3
%endmacro

; buf += -stride * h + off
; OFFSET_TO_W4(buf, stride, off)
%macro OFFSET_TO_W4 3
    mov                    id, hd
    imul                   iq, %2
    sub                    %1, iq
    lea                    %1, [%1 + %3]
%endmacro

%macro OFFSET_TO_W4 0
    OFFSET_TO_W4         srcq, ssq, 16 * (bd / 10 + 1)
    OFFSET_TO_W4         dstq, dsq, 16 * 2
%endmacro

; void ff_vvc_dmvr_%1_avx2(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
;     int height, intptr_t mx, intptr_t my, int width);
%macro DMVR_AVX2 1
cglobal vvc_dmvr_%1, 4, 9, 5, dst, src, ss, h, ds, ds3, w, ss3, i
%define bd %1

    LOAD_STRIDES

%if %1 > 10
    vpbroadcastd          m4, [shift_%1]
%endif

    mov                   wd, wm
    mov                   id, hd
.w16:
    sub                   id, 4
    jl              .w16_end
    DMVR_4xW16          dstq, dsq, ds3q, srcq, ssq, ss3q
    NEXT_4_LINES           0
    jmp                 .w16
.w16_end:

    sub                   wd, 16
    jl               .w4_end

    OFFSET_TO_W4
.w4:
    sub                   hd, 4
    jl               .w4_end
    DMVR_4xW16          dstq, dsq, ds3q, srcq, ssq, ss3q
    NEXT_4_LINES           0
    jmp                 .w4
.w4_end:

    RET
%endmacro

; LOAD_COEFFS(coeffs0, coeffs1, src)
%macro LOAD_COEFFS 3
    movd                xm%2, %3
    vpbroadcastw         m%2, xm%2
    vpbroadcastd         m%1, [pw_16]
    psubw                m%1, m%2
%endmacro

; LOAD_SHIFT(shift, src)
%macro LOAD_SHIFT 2
    vpbroadcastd           %1, [%2]
%if bd == 12
    psllw                  %1, 1                        ; avoid signed mul for pmulhrsw
%endif
%endmacro

; LOAD_STRIDES(shift, src)
%macro LOAD_STRIDES 0
    mov                  dsq, MAX_PB_SIZE * 2
    lea                 ss3q, [ssq*3]
    lea                 ds3q, [dsq*3]
%endmacro

; BILINEAR(dst/src0, src1, coeff0, coeff1, round, tmp)
%macro BILINEAR 6
    pmullw                 %1, %3
    pmullw                 %6, %2, %4
    paddw                  %1, %6
%if bd == 12
    psrlw                  %1, 1                        ; avoid signed mul for pmulhrsw
%endif
    pmulhrsw               %1, %5
%endmacro

; DMVR_H_1xW16(dst, src0, src1, offset, tmp)
%macro DMVR_H_1xW16 5
    LOAD_W16               %1, [%2 + %4]
    LOAD_W16               %5, [%3 + %4]
    BILINEAR               %1, %5, m10, m11, m12, %5
%endmacro

; DMVR_H_4xW16(dst, dst_stride, dst_stride3, src, src_stride, src_stride3, src1)
%macro DMVR_H_4xW16 7
    DMVR_H_1xW16           m0, %4, %7,      0, m4
    DMVR_H_1xW16           m1, %4, %7,     %5, m5
    DMVR_H_1xW16           m2, %4, %7, 2 * %5, m6
    DMVR_H_1xW16           m3, %4, %7,     %6, m7

    SAVE_W16    [%1]         , m0
    SAVE_W16    [%1 + %2]    , m1
    SAVE_W16    [%1 + 2 * %2], m2
    SAVE_W16    [%1 + %3]    , m3
%endmacro

; void ff_vvc_dmvr_h_%1_avx2(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
;     int height, intptr_t mx, intptr_t my, int width);
%macro DMVR_H_AVX2 1
cglobal vvc_dmvr_h_%1, 4, 10, 13, dst, src, ss, h, ds, ds3, w, ss3, src1, i
%define bd %1

    LOAD_COEFFS           10, 11, dsm
    LOAD_SHIFT           m12, shift3_%1

    LOAD_STRIDES
    lea                src1q, [srcq + pstride]

    mov                   wd, wm
    mov                   id, hd
.w16:
    sub                   id, 4
    jl              .w16_end
    DMVR_H_4xW16        dstq, dsq, ds3q, srcq, ssq, ss3q, src1q
    NEXT_4_LINES           1
    jmp                 .w16
.w16_end:

    sub                   wd, 16
    jl               .w4_end

    OFFSET_TO_W4
    lea                src1q, [srcq + pstride]
.w4:
    sub                   hd, 4
    jl               .w4_end
    DMVR_H_4xW16        dstq, dsq, ds3q, srcq, ssq, ss3q, src1q
    NEXT_4_LINES           1
    jmp                 .w4
.w4_end:

    RET
%endmacro

; DMVR_V_4xW16(dst, dst_stride, dst_stride3, src, src_stride, src_stride3)
%macro DMVR_V_4xW16 6
    LOAD_W16               m1, [%4 + %5]
    LOAD_W16               m2, [%4 + 2 * %5]
    LOAD_W16               m3, [%4 + %6]
    LOAD_W16               m4, [%4 + 4 * %5]

    BILINEAR               m0, m1, m8, m9, m10, m11
    BILINEAR               m1, m2, m8, m9, m10, m12
    BILINEAR               m2, m3, m8, m9, m10, m13
    BILINEAR               m3, m4, m8, m9, m10, m14

    SAVE_W16    [%1]         , m0
    SAVE_W16    [%1 + %2]    , m1
    SAVE_W16    [%1 + 2 * %2], m2
    SAVE_W16    [%1 + %3]    , m3

    ; why can't we use SWAP m0, m4 here?
    movaps                 m0, m4
%endmacro

; void ff_vvc_dmvr_v_%1_avx2(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
;     int height, intptr_t mx, intptr_t my, int width);
%macro DMVR_V_AVX2 1
cglobal vvc_dmvr_v_%1, 4, 9, 15, dst, src, ss, h, ds, ds3, w, ss3, i
%define bd %1

    LOAD_COEFFS            8, 9, ds3m
    LOAD_SHIFT           m10, shift3_%1

    LOAD_STRIDES

    mov                   wd, wm
    mov                   id, hd
    LOAD_W16              m0, [srcq]
.w16:
    sub                   id, 4
    jl              .w16_end
    DMVR_V_4xW16        dstq, dsq, ds3q, srcq, ssq, ss3q
    NEXT_4_LINES           0
    jmp                 .w16
.w16_end:

    sub                   wd, 16
    jl               .w4_end

    OFFSET_TO_W4
    LOAD_W16              m0, [srcq]
.w4:
    sub                   hd, 4
    jl               .w4_end
    DMVR_V_4xW16        dstq, dsq, ds3q, srcq, ssq, ss3q
    NEXT_4_LINES           0
    jmp                 .w4
.w4_end:

    RET
%endmacro

; DMVR_HV_4xW16(dst, dst_stride, dst_stride3, src, src_stride, src_stride3, src1)
%macro DMVR_HV_4xW16 7
    DMVR_H_1xW16           m1, %4, %7,     %5, m6
    DMVR_H_1xW16           m2, %4, %7, 2 * %5, m7
    DMVR_H_1xW16           m3, %4, %7,     %6, m8
    DMVR_H_1xW16           m4, %4, %7, 4 * %5, m9

    BILINEAR               m0, m1, m13, m14, m15, m6
    BILINEAR               m1, m2, m13, m14, m15, m7
    BILINEAR               m2, m3, m13, m14, m15, m8
    BILINEAR               m3, m4, m13, m14, m15, m9

    SAVE_W16    [%1]         , m0
    SAVE_W16    [%1 + %2]    , m1
    SAVE_W16    [%1 + 2 * %2], m2
    SAVE_W16    [%1 + %3]    , m3

    ; why can't we use SWAP m0, m4 here?
    movaps                 m0, m4
%endmacro

; void ff_vvc_dmvr_hv_%1_avx2(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
;     int height, intptr_t mx, intptr_t my, int width);
%macro DMVR_HV_AVX2 1
cglobal vvc_dmvr_hv_%1, 7, 10, 16, dst, src, ss, h, ds, ds3, w, ss3, src1, i
%define bd %1

    LOAD_COEFFS           10, 11, dsm
    LOAD_SHIFT           m12, shift3_%1

    LOAD_COEFFS           13, 14, ds3m
    LOAD_SHIFT           m15, shift3_10

    LOAD_STRIDES
    lea                src1q, [srcq + pstride]

    mov                   id, hd
    DMVR_H_1xW16          m0, srcq, src1q, 0, m5
.w16:
    sub                   id, 4
    jl              .w16_end
    DMVR_HV_4xW16       dstq, dsq, ds3q, srcq, ssq, ss3q, src1q
    NEXT_4_LINES           1
    jmp                 .w16
.w16_end:

    sub                   wd, 16
    jl               .w4_end

    OFFSET_TO_W4
    lea                src1q, [srcq + pstride]

    DMVR_H_1xW16          m0, srcq, src1q, 0, m5
.w4:
    sub                   hd, 4
    jl               .w4_end
    DMVR_HV_4xW16       dstq, dsq, ds3q, srcq, ssq, ss3q, src1q
    NEXT_4_LINES           1
    jmp                 .w4
.w4_end:

    RET
%endmacro

%macro VVC_DMVR_AVX2 1
    DMVR_AVX2    %1
    DMVR_H_AVX2  %1
    DMVR_V_AVX2  %1
    DMVR_HV_AVX2 %1
%endmacro

INIT_YMM avx2

VVC_DMVR_AVX2 8
VVC_DMVR_AVX2 10
VVC_DMVR_AVX2 12

%endif ; HAVE_AVX2_EXTERNAL

%endif ; ARCH_X86_64
