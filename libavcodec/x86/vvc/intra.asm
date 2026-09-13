;******************************************************************************
;* SIMD-optimized VVC intra prediction
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

%if ARCH_X86_64
%if HAVE_AVX2_EXTERNAL

SECTION_RODATA 32

; (w - 1 - x) is a suffix of pw_desc, (x + 1) a prefix of pw_asc. The tail keeps
; the w = 4 group, which is read eight words wide, inside the table.
pw_desc: dw 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48
         dw 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32
         dw 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16
         dw 15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0
         dw  0,  0,  0,  0,  0,  0,  0,  0
pw_asc:  dw  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16
         dw 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32
         dw 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48
         dw 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64

cextern pw_255

SECTION .text

;-----------------------------------------------------------------------------
; void ff_vvc_pred_planar_w<w>_8_avx2(uint8_t *src, const uint8_t *top,
;                                     const uint8_t *left, int w, int h,
;                                     ptrdiff_t stride)
;-----------------------------------------------------------------------------

; src[y*stride + x] = (A << logw) + (B << logh) + w*h >> (logw + logh + 1)
;   A = (h-1-y)*top[x] + (y+1)*left[h]
;   B = (w-1-x)*left[y] + (x+1)*top[w]
;
; Factoring 2^min(logw,logh) out of the numerator gives
;   pred = ((A << sa) + (B << sb) + max(w,h)) >> (sc + 1)
; with sc = max(logw,logh), sa = logw - min, sb = logh - min, so one shift is
; always zero. (A << sa) and (B << sb) are each at most 255*max(w,h), so the
; numerator peaks at 511*max(w,h) = 32704 and the kernel stays in words. A is
; linear in y with the loop-invariant step left[h] - top[x], so it is carried
; as a running accumulator and a row costs one pmullw for B plus three shifts.

; Set up the running accumulator A(0) and its per-row step for the columns
; starting at %4. Clobbers m6 and m7.
; %1 = accumulator, %2 = step, %3 = (x+1)*top[w], %4 = first column, %5 = width
%macro PLANAR_INIT 5
    vpbroadcastb    %3, [topq + %5]
    pand            %3, m5                      ; top[w]
    pmullw          %3, [pw_asc + (%4) * 2]     ; (x+1)*top[w]
    vpbroadcastb    %1, [leftq + hq]
    pand            %1, m5                      ; left[h]
    pmovzxbw        m7, [topq + %4]             ; top[x]
    psubw           %2, %1, m7                  ; step = left[h] - top[x]
    lea           tmpd, [hq - 1]
    movd           xm6, tmpd
    vpbroadcastw    m6, xm6
    pmullw          m6, m7
    paddw           %1, m6                      ; A(0)
%endmacro

; One row of one group: %1 = A, %2 = (x+1)*top[w], %3 = left[y] (preserved),
; %4 = first column, %5 = width, %6 = output
%macro PLANAR_ROW 6
    pmullw          m6, %3, [pw_desc + (64 - %5 + %4) * 2]
    paddw           m6, %2                      ; B
    psllw           m6, xm9                     ; B << sb
    psllw           %6, %1, xm8                 ; A << sa
    paddw           %6, m6
    paddw           %6, m4
    psrlw           %6, xm10
%endmacro

; One group of mmsize/2 columns, for widths of at most that.
; %1 = width, %2 = first column
%macro PLANAR_GROUP 2
    PLANAR_INIT     m0, m1, m2, %2, %1
    lea           srcq, [baseq + %2]
    xor             yd, yd
%%loop:
    vpbroadcastb    m3, [leftq + yq]
    pand            m3, m5                      ; left[y]
    PLANAR_ROW      m0, m2, m3, %2, %1, m7
    packuswb        m7, m7
%if %1 == 4
    movd        [srcq], xm7
%elif %1 == 8
    movq        [srcq], xm7
%else
    vpermq          m7, m7, q3120
    movu        [srcq], xm7
%endif
    add           srcq, strideq
    paddw           m0, m1
    inc             yd
    cmp             yd, hd
    jl %%loop
%endmacro

; Two adjacent groups of 16 columns per pass, so that the left[y] broadcast
; is shared and a row is written with a single 32 byte store.
; %1 = width, %2 = first column
%macro PLANAR_GROUP2 2
    PLANAR_INIT     m0,  m1,  m2,  %2,      %1
    PLANAR_INIT     m11, m12, m13, %2 + 16, %1
    lea           srcq, [baseq + %2]
    xor             yd, yd
%%loop:
    vpbroadcastb    m3, [leftq + yq]
    pand            m3, m5                      ; left[y]
    PLANAR_ROW      m0,  m2,  m3, %2,      %1, m7
    PLANAR_ROW      m11, m13, m3, %2 + 16, %1, m14
    packuswb        m7, m14
    vpermq          m7, m7, q3120
    movu        [srcq], m7
    add           srcq, strideq
    paddw           m0, m1
    paddw          m11, m12
    inc             yd
    cmp             yd, hd
    jl %%loop
%endmacro

; %1 = width, %2 = log2(width)
%macro PRED_PLANAR 2
cglobal vvc_pred_planar_w%1_8, 6, 11, 11 + 4 * (%1 > 16), src, top, left, w, h, stride, base, y, tmp, sha, shb
    movsxdifnidn    hq, hd
    bsr           tmpd, hd                      ; logh
    mov          shad, %2
    sub          shad, tmpd                     ; logw - logh
    mov          shbd, tmpd
    sub          shbd, %2                       ; logh - logw
    xor           tmpd, tmpd
    test         shad, shad
    cmovs        shad, tmpd
    test         shbd, shbd
    cmovs        shbd, tmpd
    movd           xm8, shad
    movd           xm9, shbd
    mov           tmpd, %1
    cmp           tmpd, hd
    cmovl         tmpd, hd                      ; max(w,h) = 1 << sc
    movd           xm4, tmpd
    vpbroadcastw    m4, xm4
    bsr           tmpd, tmpd
    inc           tmpd
    movd          xm10, tmpd                    ; sc + 1
    mova            m5, [pw_255]
    mov          baseq, srcq
%if %1 <= 16
    PLANAR_GROUP %1, 0
%else
%assign %%c 0
%rep %1 / 32
    PLANAR_GROUP2 %1, %%c
%assign %%c %%c + 32
%endrep
%endif
    RET
%endmacro

INIT_XMM avx2
PRED_PLANAR  4, 2
PRED_PLANAR  8, 3
INIT_YMM avx2
PRED_PLANAR 16, 4
PRED_PLANAR 32, 5
PRED_PLANAR 64, 6

%endif ; HAVE_AVX2_EXTERNAL
%endif ; ARCH_X86_64
