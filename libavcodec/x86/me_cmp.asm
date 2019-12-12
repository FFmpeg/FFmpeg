;*****************************************************************************
;* SIMD-optimized motion compensation estimation
;*****************************************************************************
;* Copyright (c) 2000, 2001 Fabrice Bellard
;* Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
;*****************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

cextern pb_1
cextern pb_80

SECTION .text

%macro DIFF_PIXELS_1 4
    movh            %1, %3
    movh            %2, %4
    punpcklbw       %2, %1
    punpcklbw       %1, %1
    psubw           %1, %2
%endmacro

; %1=uint8_t *pix1, %2=uint8_t *pix2, %3=static offset, %4=stride, %5=stride*3
; %6=temporary storage location
; this macro requires $mmsize stack space (aligned) on %6 (except on SSE+x86-64)
%macro DIFF_PIXELS_8 6
    DIFF_PIXELS_1   m0, m7, [%1     +%3], [%2     +%3]
    DIFF_PIXELS_1   m1, m7, [%1+%4  +%3], [%2+%4  +%3]
    DIFF_PIXELS_1   m2, m7, [%1+%4*2+%3], [%2+%4*2+%3]
    add             %1, %5
    add             %2, %5
    DIFF_PIXELS_1   m3, m7, [%1     +%3], [%2     +%3]
    DIFF_PIXELS_1   m4, m7, [%1+%4  +%3], [%2+%4  +%3]
    DIFF_PIXELS_1   m5, m7, [%1+%4*2+%3], [%2+%4*2+%3]
    DIFF_PIXELS_1   m6, m7, [%1+%5  +%3], [%2+%5  +%3]
%ifdef m8
    DIFF_PIXELS_1   m7, m8, [%1+%4*4+%3], [%2+%4*4+%3]
%else
    mova          [%6], m0
    DIFF_PIXELS_1   m7, m0, [%1+%4*4+%3], [%2+%4*4+%3]
    mova            m0, [%6]
%endif
    sub             %1, %5
    sub             %2, %5
%endmacro

%macro HADAMARD8 0
    SUMSUB_BADC       w, 0, 1, 2, 3
    SUMSUB_BADC       w, 4, 5, 6, 7
    SUMSUB_BADC       w, 0, 2, 1, 3
    SUMSUB_BADC       w, 4, 6, 5, 7
    SUMSUB_BADC       w, 0, 4, 1, 5
    SUMSUB_BADC       w, 2, 6, 3, 7
%endmacro

%macro ABS1_SUM 3
    ABS1            %1, %2
    paddusw         %3, %1
%endmacro

%macro ABS2_SUM 6
    ABS2            %1, %2, %3, %4
    paddusw         %5, %1
    paddusw         %6, %2
%endmacro

%macro ABS_SUM_8x8_64 1
    ABS2            m0, m1, m8, m9
    ABS2_SUM        m2, m3, m8, m9, m0, m1
    ABS2_SUM        m4, m5, m8, m9, m0, m1
    ABS2_SUM        m6, m7, m8, m9, m0, m1
    paddusw         m0, m1
%endmacro

%macro ABS_SUM_8x8_32 1
    mova          [%1], m7
    ABS1            m0, m7
    ABS1            m1, m7
    ABS1_SUM        m2, m7, m0
    ABS1_SUM        m3, m7, m1
    ABS1_SUM        m4, m7, m0
    ABS1_SUM        m5, m7, m1
    ABS1_SUM        m6, m7, m0
    mova            m2, [%1]
    ABS1_SUM        m2, m7, m1
    paddusw         m0, m1
%endmacro

; FIXME: HSUM saturates at 64k, while an 8x8 hadamard or dct block can get up to
; about 100k on extreme inputs. But that's very unlikely to occur in natural video,
; and it's even more unlikely to not have any alternative mvs/modes with lower cost.
%macro HSUM 3
%if cpuflag(sse2)
    movhlps         %2, %1
    paddusw         %1, %2
    pshuflw         %2, %1, 0xE
    paddusw         %1, %2
    pshuflw         %2, %1, 0x1
    paddusw         %1, %2
    movd            %3, %1
%elif cpuflag(mmxext)
    pshufw          %2, %1, 0xE
    paddusw         %1, %2
    pshufw          %2, %1, 0x1
    paddusw         %1, %2
    movd            %3, %1
%elif cpuflag(mmx)
    mova            %2, %1
    psrlq           %1, 32
    paddusw         %1, %2
    mova            %2, %1
    psrlq           %1, 16
    paddusw         %1, %2
    movd            %3, %1
%endif
%endmacro

%macro STORE4 5
    mova [%1+mmsize*0], %2
    mova [%1+mmsize*1], %3
    mova [%1+mmsize*2], %4
    mova [%1+mmsize*3], %5
%endmacro

%macro LOAD4 5
    mova            %2, [%1+mmsize*0]
    mova            %3, [%1+mmsize*1]
    mova            %4, [%1+mmsize*2]
    mova            %5, [%1+mmsize*3]
%endmacro

%macro hadamard8_16_wrapper 2
cglobal hadamard8_diff, 4, 4, %1
%ifndef m8
    %assign pad %2*mmsize-(4+stack_offset&(mmsize-1))
    SUB            rsp, pad
%endif
    call hadamard8x8_diff %+ SUFFIX
%ifndef m8
    ADD            rsp, pad
%endif
    RET

cglobal hadamard8_diff16, 5, 6, %1
%ifndef m8
    %assign pad %2*mmsize-(4+stack_offset&(mmsize-1))
    SUB            rsp, pad
%endif

    call hadamard8x8_diff %+ SUFFIX
    mov            r5d, eax

    add             r1, 8
    add             r2, 8
    call hadamard8x8_diff %+ SUFFIX
    add            r5d, eax

    cmp            r4d, 16
    jne .done

    lea             r1, [r1+r3*8-8]
    lea             r2, [r2+r3*8-8]
    call hadamard8x8_diff %+ SUFFIX
    add            r5d, eax

    add             r1, 8
    add             r2, 8
    call hadamard8x8_diff %+ SUFFIX
    add            r5d, eax

.done:
    mov            eax, r5d
%ifndef m8
    ADD            rsp, pad
%endif
    RET
%endmacro

%macro HADAMARD8_DIFF 0-1
%if cpuflag(sse2)
hadamard8x8_diff %+ SUFFIX:
    lea                          r0, [r3*3]
    DIFF_PIXELS_8                r1, r2,  0, r3, r0, rsp+gprsize
    HADAMARD8
%if ARCH_X86_64
    TRANSPOSE8x8W                 0,  1,  2,  3,  4,  5,  6,  7,  8
%else
    TRANSPOSE8x8W                 0,  1,  2,  3,  4,  5,  6,  7, [rsp+gprsize], [rsp+mmsize+gprsize]
%endif
    HADAMARD8
    ABS_SUM_8x8         rsp+gprsize
    HSUM                        m0, m1, eax
    and                         eax, 0xFFFF
    ret

hadamard8_16_wrapper %1, 3
%elif cpuflag(mmx)
ALIGN 16
; int ff_hadamard8_diff_ ## cpu(MpegEncContext *s, uint8_t *src1,
;                               uint8_t *src2, ptrdiff_t stride, int h)
; r0 = void *s = unused, int h = unused (always 8)
; note how r1, r2 and r3 are not clobbered in this function, so 16x16
; can simply call this 2x2x (and that's why we access rsp+gprsize
; everywhere, which is rsp of calling func
hadamard8x8_diff %+ SUFFIX:
    lea                          r0, [r3*3]

    ; first 4x8 pixels
    DIFF_PIXELS_8                r1, r2,  0, r3, r0, rsp+gprsize+0x60
    HADAMARD8
    mova         [rsp+gprsize+0x60], m7
    TRANSPOSE4x4W                 0,  1,  2,  3,  7
    STORE4              rsp+gprsize, m0, m1, m2, m3
    mova                         m7, [rsp+gprsize+0x60]
    TRANSPOSE4x4W                 4,  5,  6,  7,  0
    STORE4         rsp+gprsize+0x40, m4, m5, m6, m7

    ; second 4x8 pixels
    DIFF_PIXELS_8                r1, r2,  4, r3, r0, rsp+gprsize+0x60
    HADAMARD8
    mova         [rsp+gprsize+0x60], m7
    TRANSPOSE4x4W                 0,  1,  2,  3,  7
    STORE4         rsp+gprsize+0x20, m0, m1, m2, m3
    mova                         m7, [rsp+gprsize+0x60]
    TRANSPOSE4x4W                 4,  5,  6,  7,  0

    LOAD4          rsp+gprsize+0x40, m0, m1, m2, m3
    HADAMARD8
    ABS_SUM_8x8_32 rsp+gprsize+0x60
    mova         [rsp+gprsize+0x60], m0

    LOAD4          rsp+gprsize     , m0, m1, m2, m3
    LOAD4          rsp+gprsize+0x20, m4, m5, m6, m7
    HADAMARD8
    ABS_SUM_8x8_32 rsp+gprsize
    paddusw                      m0, [rsp+gprsize+0x60]

    HSUM                         m0, m1, eax
    and                         rax, 0xFFFF
    ret

hadamard8_16_wrapper 0, 14
%endif
%endmacro

INIT_MMX mmx
HADAMARD8_DIFF

INIT_MMX mmxext
HADAMARD8_DIFF

INIT_XMM sse2
%if ARCH_X86_64
%define ABS_SUM_8x8 ABS_SUM_8x8_64
%else
%define ABS_SUM_8x8 ABS_SUM_8x8_32
%endif
HADAMARD8_DIFF 10

INIT_XMM ssse3
%define ABS_SUM_8x8 ABS_SUM_8x8_64
HADAMARD8_DIFF 9

; int ff_sse*_*(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
;               ptrdiff_t line_size, int h)

%macro SUM_SQUARED_ERRORS 1
cglobal sse%1, 5,5,8, v, pix1, pix2, lsize, h
%if %1 == mmsize
    shr       hd, 1
%endif
    pxor      m0, m0         ; mm0 = 0
    pxor      m7, m7         ; mm7 holds the sum

.next2lines: ; FIXME why are these unaligned movs? pix1[] is aligned
    movu      m1, [pix1q]    ; m1 = pix1[0][0-15], [0-7] for mmx
    movu      m2, [pix2q]    ; m2 = pix2[0][0-15], [0-7] for mmx
%if %1 == mmsize
    movu      m3, [pix1q+lsizeq] ; m3 = pix1[1][0-15], [0-7] for mmx
    movu      m4, [pix2q+lsizeq] ; m4 = pix2[1][0-15], [0-7] for mmx
%else  ; %1 / 2 == mmsize; mmx only
    mova      m3, [pix1q+8]  ; m3 = pix1[0][8-15]
    mova      m4, [pix2q+8]  ; m4 = pix2[0][8-15]
%endif

    ; todo: mm1-mm2, mm3-mm4
    ; algo: subtract mm1 from mm2 with saturation and vice versa
    ;       OR the result to get the absolute difference
    mova      m5, m1
    mova      m6, m3
    psubusb   m1, m2
    psubusb   m3, m4
    psubusb   m2, m5
    psubusb   m4, m6

    por       m2, m1
    por       m4, m3

    ; now convert to 16-bit vectors so we can square them
    mova      m1, m2
    mova      m3, m4

    punpckhbw m2, m0
    punpckhbw m4, m0
    punpcklbw m1, m0         ; mm1 not spread over (mm1,mm2)
    punpcklbw m3, m0         ; mm4 not spread over (mm3,mm4)

    pmaddwd   m2, m2
    pmaddwd   m4, m4
    pmaddwd   m1, m1
    pmaddwd   m3, m3

    paddd     m1, m2
    paddd     m3, m4
    paddd     m7, m1
    paddd     m7, m3

%if %1 == mmsize
    lea    pix1q, [pix1q + 2*lsizeq]
    lea    pix2q, [pix2q + 2*lsizeq]
%else
    add    pix1q, lsizeq
    add    pix2q, lsizeq
%endif
    dec       hd
    jnz .next2lines

    HADDD     m7, m1
    movd     eax, m7         ; return value
    RET
%endmacro

INIT_MMX mmx
SUM_SQUARED_ERRORS 8

INIT_MMX mmx
SUM_SQUARED_ERRORS 16

INIT_XMM sse2
SUM_SQUARED_ERRORS 16

;-----------------------------------------------
;int ff_sum_abs_dctelem(int16_t *block)
;-----------------------------------------------
; %1 = number of xmm registers used
; %2 = number of inline loops

%macro SUM_ABS_DCTELEM 2
cglobal sum_abs_dctelem, 1, 1, %1, block
    pxor    m0, m0
    pxor    m1, m1
%assign %%i 0
%rep %2
    mova      m2, [blockq+mmsize*(0+%%i)]
    mova      m3, [blockq+mmsize*(1+%%i)]
    mova      m4, [blockq+mmsize*(2+%%i)]
    mova      m5, [blockq+mmsize*(3+%%i)]
    ABS1_SUM  m2, m6, m0
    ABS1_SUM  m3, m6, m1
    ABS1_SUM  m4, m6, m0
    ABS1_SUM  m5, m6, m1
%assign %%i %%i+4
%endrep
    paddusw m0, m1
    HSUM    m0, m1, eax
    and     eax, 0xFFFF
    RET
%endmacro

INIT_MMX mmx
SUM_ABS_DCTELEM 0, 4
INIT_MMX mmxext
SUM_ABS_DCTELEM 0, 4
INIT_XMM sse2
SUM_ABS_DCTELEM 7, 2
INIT_XMM ssse3
SUM_ABS_DCTELEM 6, 2

;------------------------------------------------------------------------------
; int ff_hf_noise*_mmx(uint8_t *pix1, ptrdiff_t lsize, int h)
;------------------------------------------------------------------------------
; %1 = 8/16. %2-5=m#
%macro HF_NOISE_PART1 5
    mova      m%2, [pix1q]
%if %1 == 8
    mova      m%3, m%2
    psllq     m%2, 8
    psrlq     m%3, 8
    psrlq     m%2, 8
%else
    mova      m%3, [pix1q+1]
%endif
    mova      m%4, m%2
    mova      m%5, m%3
    punpcklbw m%2, m7
    punpcklbw m%3, m7
    punpckhbw m%4, m7
    punpckhbw m%5, m7
    psubw     m%2, m%3
    psubw     m%4, m%5
%endmacro

; %1-2 = m#
%macro HF_NOISE_PART2 4
    psubw     m%1, m%3
    psubw     m%2, m%4
    pxor       m3, m3
    pxor       m1, m1
    pcmpgtw    m3, m%1
    pcmpgtw    m1, m%2
    pxor      m%1, m3
    pxor      m%2, m1
    psubw     m%1, m3
    psubw     m%2, m1
    paddw     m%2, m%1
    paddw      m6, m%2
%endmacro

; %1 = 8/16
%macro HF_NOISE 1
cglobal hf_noise%1, 3,3,0, pix1, lsize, h
    sub        hd, 2
    pxor       m7, m7
    pxor       m6, m6
    HF_NOISE_PART1 %1, 0, 1, 2, 3
    add     pix1q, lsizeq
    HF_NOISE_PART1 %1, 4, 1, 5, 3
    HF_NOISE_PART2     0, 2, 4, 5
    add     pix1q, lsizeq
.loop:
    HF_NOISE_PART1 %1, 0, 1, 2, 3
    HF_NOISE_PART2     4, 5, 0, 2
    add     pix1q, lsizeq
    HF_NOISE_PART1 %1, 4, 1, 5, 3
    HF_NOISE_PART2     0, 2, 4, 5
    add     pix1q, lsizeq
    sub        hd, 2
        jne .loop

    mova       m0, m6
    punpcklwd  m0, m7
    punpckhwd  m6, m7
    paddd      m6, m0
    mova       m0, m6
    psrlq      m6, 32
    paddd      m0, m6
    movd      eax, m0   ; eax = result of hf_noise8;
    REP_RET                 ; return eax;
%endmacro

INIT_MMX mmx
HF_NOISE 8
HF_NOISE 16

;---------------------------------------------------------------------------------------
;int ff_sad_<opt>(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2, ptrdiff_t stride, int h);
;---------------------------------------------------------------------------------------
;%1 = 8/16
%macro SAD 1
cglobal sad%1, 5, 5, 3, v, pix1, pix2, stride, h
    movu      m2, [pix2q]
    movu      m1, [pix2q+strideq]
    psadbw    m2, [pix1q]
    psadbw    m1, [pix1q+strideq]
    paddw     m2, m1
%if %1 != mmsize
    movu      m0, [pix2q+8]
    movu      m1, [pix2q+strideq+8]
    psadbw    m0, [pix1q+8]
    psadbw    m1, [pix1q+strideq+8]
    paddw     m2, m0
    paddw     m2, m1
%endif
    sub       hd, 2

align 16
.loop:
    lea    pix1q, [pix1q+strideq*2]
    lea    pix2q, [pix2q+strideq*2]
    movu      m0, [pix2q]
    movu      m1, [pix2q+strideq]
    psadbw    m0, [pix1q]
    psadbw    m1, [pix1q+strideq]
    paddw     m2, m0
    paddw     m2, m1
%if %1 != mmsize
    movu      m0, [pix2q+8]
    movu      m1, [pix2q+strideq+8]
    psadbw    m0, [pix1q+8]
    psadbw    m1, [pix1q+strideq+8]
    paddw     m2, m0
    paddw     m2, m1
%endif
    sub       hd, 2
    jg .loop
%if mmsize == 16
    movhlps   m0, m2
    paddw     m2, m0
%endif
    movd     eax, m2
    RET
%endmacro

INIT_MMX mmxext
SAD 8
SAD 16
INIT_XMM sse2
SAD 16

;------------------------------------------------------------------------------------------
;int ff_sad_x2_<opt>(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2, ptrdiff_t stride, int h);
;------------------------------------------------------------------------------------------
;%1 = 8/16
%macro SAD_X2 1
cglobal sad%1_x2, 5, 5, 5, v, pix1, pix2, stride, h
    movu      m0, [pix2q]
    movu      m2, [pix2q+strideq]
%if mmsize == 16
    movu      m3, [pix2q+1]
    movu      m4, [pix2q+strideq+1]
    pavgb     m0, m3
    pavgb     m2, m4
%else
    pavgb     m0, [pix2q+1]
    pavgb     m2, [pix2q+strideq+1]
%endif
    psadbw    m0, [pix1q]
    psadbw    m2, [pix1q+strideq]
    paddw     m0, m2
%if %1 != mmsize
    movu      m1, [pix2q+8]
    movu      m2, [pix2q+strideq+8]
    pavgb     m1, [pix2q+9]
    pavgb     m2, [pix2q+strideq+9]
    psadbw    m1, [pix1q+8]
    psadbw    m2, [pix1q+strideq+8]
    paddw     m0, m1
    paddw     m0, m2
%endif
    sub       hd, 2

align 16
.loop:
    lea    pix1q, [pix1q+2*strideq]
    lea    pix2q, [pix2q+2*strideq]
    movu      m1, [pix2q]
    movu      m2, [pix2q+strideq]
%if mmsize == 16
    movu      m3, [pix2q+1]
    movu      m4, [pix2q+strideq+1]
    pavgb     m1, m3
    pavgb     m2, m4
%else
    pavgb     m1, [pix2q+1]
    pavgb     m2, [pix2q+strideq+1]
%endif
    psadbw    m1, [pix1q]
    psadbw    m2, [pix1q+strideq]
    paddw     m0, m1
    paddw     m0, m2
%if %1 != mmsize
    movu      m1, [pix2q+8]
    movu      m2, [pix2q+strideq+8]
    pavgb     m1, [pix2q+9]
    pavgb     m2, [pix2q+strideq+9]
    psadbw    m1, [pix1q+8]
    psadbw    m2, [pix1q+strideq+8]
    paddw     m0, m1
    paddw     m0, m2
%endif
    sub       hd, 2
    jg .loop
%if mmsize == 16
    movhlps   m1, m0
    paddw     m0, m1
%endif
    movd     eax, m0
    RET
%endmacro

INIT_MMX mmxext
SAD_X2 8
SAD_X2 16
INIT_XMM sse2
SAD_X2 16

;------------------------------------------------------------------------------------------
;int ff_sad_y2_<opt>(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2, ptrdiff_t stride, int h);
;------------------------------------------------------------------------------------------
;%1 = 8/16
%macro SAD_Y2 1
cglobal sad%1_y2, 5, 5, 4, v, pix1, pix2, stride, h
    movu      m1, [pix2q]
    movu      m0, [pix2q+strideq]
    movu      m3, [pix2q+2*strideq]
    pavgb     m1, m0
    pavgb     m0, m3
    psadbw    m1, [pix1q]
    psadbw    m0, [pix1q+strideq]
    paddw     m0, m1
    mova      m1, m3
%if %1 != mmsize
    movu      m4, [pix2q+8]
    movu      m5, [pix2q+strideq+8]
    movu      m6, [pix2q+2*strideq+8]
    pavgb     m4, m5
    pavgb     m5, m6
    psadbw    m4, [pix1q+8]
    psadbw    m5, [pix1q+strideq+8]
    paddw     m0, m4
    paddw     m0, m5
    mova      m4, m6
%endif
    add    pix2q, strideq
    sub       hd, 2

align 16
.loop:
    lea    pix1q, [pix1q+2*strideq]
    lea    pix2q, [pix2q+2*strideq]
    movu      m2, [pix2q]
    movu      m3, [pix2q+strideq]
    pavgb     m1, m2
    pavgb     m2, m3
    psadbw    m1, [pix1q]
    psadbw    m2, [pix1q+strideq]
    paddw     m0, m1
    paddw     m0, m2
    mova      m1, m3
%if %1 != mmsize
    movu      m5, [pix2q+8]
    movu      m6, [pix2q+strideq+8]
    pavgb     m4, m5
    pavgb     m5, m6
    psadbw    m4, [pix1q+8]
    psadbw    m5, [pix1q+strideq+8]
    paddw     m0, m4
    paddw     m0, m5
    mova      m4, m6
%endif
    sub       hd, 2
    jg .loop
%if mmsize == 16
    movhlps   m1, m0
    paddw     m0, m1
%endif
    movd     eax, m0
    RET
%endmacro

INIT_MMX mmxext
SAD_Y2 8
SAD_Y2 16
INIT_XMM sse2
SAD_Y2 16

;-------------------------------------------------------------------------------------------
;int ff_sad_approx_xy2_<opt>(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2, ptrdiff_t stride, int h);
;-------------------------------------------------------------------------------------------
;%1 = 8/16
%macro SAD_APPROX_XY2 1
cglobal sad%1_approx_xy2, 5, 5, 7, v, pix1, pix2, stride, h
    mova      m4, [pb_1]
    movu      m1, [pix2q]
    movu      m0, [pix2q+strideq]
    movu      m3, [pix2q+2*strideq]
%if mmsize == 16
    movu      m5, [pix2q+1]
    movu      m6, [pix2q+strideq+1]
    movu      m2, [pix2q+2*strideq+1]
    pavgb     m1, m5
    pavgb     m0, m6
    pavgb     m3, m2
%else
    pavgb     m1, [pix2q+1]
    pavgb     m0, [pix2q+strideq+1]
    pavgb     m3, [pix2q+2*strideq+1]
%endif
    psubusb   m0, m4
    pavgb     m1, m0
    pavgb     m0, m3
    psadbw    m1, [pix1q]
    psadbw    m0, [pix1q+strideq]
    paddw     m0, m1
    mova      m1, m3
%if %1 != mmsize
    movu      m5, [pix2q+8]
    movu      m6, [pix2q+strideq+8]
    movu      m7, [pix2q+2*strideq+8]
    pavgb     m5, [pix2q+1+8]
    pavgb     m6, [pix2q+strideq+1+8]
    pavgb     m7, [pix2q+2*strideq+1+8]
    psubusb   m6, m4
    pavgb     m5, m6
    pavgb     m6, m7
    psadbw    m5, [pix1q+8]
    psadbw    m6, [pix1q+strideq+8]
    paddw     m0, m5
    paddw     m0, m6
    mova      m5, m7
%endif
    add    pix2q, strideq
    sub       hd, 2

align 16
.loop:
    lea    pix1q, [pix1q+2*strideq]
    lea    pix2q, [pix2q+2*strideq]
    movu      m2, [pix2q]
    movu      m3, [pix2q+strideq]
%if mmsize == 16
    movu      m5, [pix2q+1]
    movu      m6, [pix2q+strideq+1]
    pavgb     m2, m5
    pavgb     m3, m6
%else
    pavgb     m2, [pix2q+1]
    pavgb     m3, [pix2q+strideq+1]
%endif
    psubusb   m2, m4
    pavgb     m1, m2
    pavgb     m2, m3
    psadbw    m1, [pix1q]
    psadbw    m2, [pix1q+strideq]
    paddw     m0, m1
    paddw     m0, m2
    mova      m1, m3
%if %1 != mmsize
    movu      m6, [pix2q+8]
    movu      m7, [pix2q+strideq+8]
    pavgb     m6, [pix2q+8+1]
    pavgb     m7, [pix2q+strideq+8+1]
    psubusb   m6, m4
    pavgb     m5, m6
    pavgb     m6, m7
    psadbw    m5, [pix1q+8]
    psadbw    m6, [pix1q+strideq+8]
    paddw     m0, m5
    paddw     m0, m6
    mova      m5, m7
%endif
    sub       hd, 2
    jg .loop
%if mmsize == 16
    movhlps   m1, m0
    paddw     m0, m1
%endif
    movd     eax, m0
    RET
%endmacro

INIT_MMX mmxext
SAD_APPROX_XY2 8
SAD_APPROX_XY2 16
INIT_XMM sse2
SAD_APPROX_XY2 16

;--------------------------------------------------------------------
;int ff_vsad_intra(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
;                  ptrdiff_t line_size, int h);
;--------------------------------------------------------------------
; %1 = 8/16
%macro VSAD_INTRA 1
cglobal vsad_intra%1, 5, 5, 3, v, pix1, pix2, lsize, h
    mova      m0, [pix1q]
%if %1 == mmsize
    mova      m2, [pix1q+lsizeq]
    psadbw    m0, m2
%else
    mova      m2, [pix1q+lsizeq]
    mova      m3, [pix1q+8]
    mova      m4, [pix1q+lsizeq+8]
    psadbw    m0, m2
    psadbw    m3, m4
    paddw     m0, m3
%endif
    sub       hd, 2

.loop:
    lea    pix1q, [pix1q + 2*lsizeq]
%if %1 == mmsize
    mova      m1, [pix1q]
    psadbw    m2, m1
    paddw     m0, m2
    mova      m2, [pix1q+lsizeq]
    psadbw    m1, m2
    paddw     m0, m1
%else
    mova      m1, [pix1q]
    mova      m3, [pix1q+8]
    psadbw    m2, m1
    psadbw    m4, m3
    paddw     m0, m2
    paddw     m0, m4
    mova      m2, [pix1q+lsizeq]
    mova      m4, [pix1q+lsizeq+8]
    psadbw    m1, m2
    psadbw    m3, m4
    paddw     m0, m1
    paddw     m0, m3
%endif
    sub       hd, 2
    jg     .loop

%if mmsize == 16
    pshufd m1, m0, 0xe
    paddd  m0, m1
%endif
    movd eax, m0
    RET
%endmacro

INIT_MMX mmxext
VSAD_INTRA 8
VSAD_INTRA 16
INIT_XMM sse2
VSAD_INTRA 16

;---------------------------------------------------------------------
;int ff_vsad_approx(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
;                   ptrdiff_t line_size, int h);
;---------------------------------------------------------------------
; %1 = 8/16
%macro VSAD_APPROX 1
cglobal vsad%1_approx, 5, 5, 5, v, pix1, pix2, lsize, h
    mova   m1, [pb_80]
    mova   m0, [pix1q]
%if %1 == mmsize ; vsad8_mmxext, vsad16_sse2
    mova   m4, [pix1q+lsizeq]
%if mmsize == 16
    movu   m3, [pix2q]
    movu   m2, [pix2q+lsizeq]
    psubb  m0, m3
    psubb  m4, m2
%else
    psubb  m0, [pix2q]
    psubb  m4, [pix2q+lsizeq]
%endif
    pxor   m0, m1
    pxor   m4, m1
    psadbw m0, m4
%else ; vsad16_mmxext
    mova   m3, [pix1q+8]
    psubb  m0, [pix2q]
    psubb  m3, [pix2q+8]
    pxor   m0, m1
    pxor   m3, m1
    mova   m4, [pix1q+lsizeq]
    mova   m5, [pix1q+lsizeq+8]
    psubb  m4, [pix2q+lsizeq]
    psubb  m5, [pix2q+lsizeq+8]
    pxor   m4, m1
    pxor   m5, m1
    psadbw m0, m4
    psadbw m3, m5
    paddw  m0, m3
%endif
    sub    hd, 2

.loop:
    lea pix1q, [pix1q + 2*lsizeq]
    lea pix2q, [pix2q + 2*lsizeq]
    mova   m2, [pix1q]
%if %1 == mmsize ; vsad8_mmxext, vsad16_sse2
%if mmsize == 16
    movu   m3, [pix2q]
    psubb  m2, m3
%else
    psubb  m2, [pix2q]
%endif
    pxor   m2, m1
    psadbw m4, m2
    paddw  m0, m4
    mova   m4, [pix1q+lsizeq]
    movu   m3, [pix2q+lsizeq]
    psubb  m4, m3
    pxor   m4, m1
    psadbw m2, m4
    paddw  m0, m2
%else ; vsad16_mmxext
    mova   m3, [pix1q+8]
    psubb  m2, [pix2q]
    psubb  m3, [pix2q+8]
    pxor   m2, m1
    pxor   m3, m1
    psadbw m4, m2
    psadbw m5, m3
    paddw  m0, m4
    paddw  m0, m5
    mova   m4, [pix1q+lsizeq]
    mova   m5, [pix1q+lsizeq+8]
    psubb  m4, [pix2q+lsizeq]
    psubb  m5, [pix2q+lsizeq+8]
    pxor   m4, m1
    pxor   m5, m1
    psadbw m2, m4
    psadbw m3, m5
    paddw  m0, m2
    paddw  m0, m3
%endif
    sub    hd, 2
    jg  .loop

%if mmsize == 16
    pshufd m1, m0, 0xe
    paddd  m0, m1
%endif
    movd  eax, m0
    RET
%endmacro

INIT_MMX mmxext
VSAD_APPROX 8
VSAD_APPROX 16
INIT_XMM sse2
VSAD_APPROX 16
