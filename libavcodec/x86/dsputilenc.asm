;*****************************************************************************
;* MMX optimized DSP utils
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
; int ff_hadamard8_diff_ ## cpu(void *s, uint8_t *src1, uint8_t *src2,
;                               int stride, int h)
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

INIT_XMM sse2
; int ff_sse16_sse2(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
cglobal sse16, 5, 5, 8
    shr      r4d, 1
    pxor      m0, m0         ; mm0 = 0
    pxor      m7, m7         ; mm7 holds the sum

.next2lines: ; FIXME why are these unaligned movs? pix1[] is aligned
    movu      m1, [r1   ]    ; mm1 = pix1[0][0-15]
    movu      m2, [r2   ]    ; mm2 = pix2[0][0-15]
    movu      m3, [r1+r3]    ; mm3 = pix1[1][0-15]
    movu      m4, [r2+r3]    ; mm4 = pix2[1][0-15]

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

    lea       r1, [r1+r3*2]  ; pix1 += 2*line_size
    lea       r2, [r2+r3*2]  ; pix2 += 2*line_size

    paddd     m1, m2
    paddd     m3, m4
    paddd     m7, m1
    paddd     m7, m3

    dec       r4
    jnz .next2lines

    mova      m1, m7
    psrldq    m7, 8          ; shift hi qword to lo
    paddd     m7, m1
    mova      m1, m7
    psrldq    m7, 4          ; shift hi dword to lo
    paddd     m7, m1
    movd     eax, m7         ; return value
    RET

INIT_MMX mmx
; void ff_get_pixels_mmx(int16_t *block, const uint8_t *pixels, int line_size)
cglobal get_pixels, 3,4
    movsxdifnidn r2, r2d
    add          r0, 128
    mov          r3, -128
    pxor         m7, m7
.loop:
    mova         m0, [r1]
    mova         m2, [r1+r2]
    mova         m1, m0
    mova         m3, m2
    punpcklbw    m0, m7
    punpckhbw    m1, m7
    punpcklbw    m2, m7
    punpckhbw    m3, m7
    mova [r0+r3+ 0], m0
    mova [r0+r3+ 8], m1
    mova [r0+r3+16], m2
    mova [r0+r3+24], m3
    lea          r1, [r1+r2*2]
    add          r3, 32
    js .loop
    REP_RET

INIT_XMM sse2
cglobal get_pixels, 3, 4
    movsxdifnidn r2, r2d
    lea          r3, [r2*3]
    pxor         m4, m4
    movh         m0, [r1]
    movh         m1, [r1+r2]
    movh         m2, [r1+r2*2]
    movh         m3, [r1+r3]
    lea          r1, [r1+r2*4]
    punpcklbw    m0, m4
    punpcklbw    m1, m4
    punpcklbw    m2, m4
    punpcklbw    m3, m4
    mova       [r0], m0
    mova  [r0+0x10], m1
    mova  [r0+0x20], m2
    mova  [r0+0x30], m3
    movh         m0, [r1]
    movh         m1, [r1+r2*1]
    movh         m2, [r1+r2*2]
    movh         m3, [r1+r3]
    punpcklbw    m0, m4
    punpcklbw    m1, m4
    punpcklbw    m2, m4
    punpcklbw    m3, m4
    mova  [r0+0x40], m0
    mova  [r0+0x50], m1
    mova  [r0+0x60], m2
    mova  [r0+0x70], m3
    RET

INIT_MMX mmx
; void ff_diff_pixels_mmx(int16_t *block, const uint8_t *s1, const uint8_t *s2,
;                         int stride);
cglobal diff_pixels, 4,5
    movsxdifnidn r3, r3d
    pxor         m7, m7
    add          r0,  128
    mov          r4, -128
.loop:
    mova         m0, [r1]
    mova         m2, [r2]
    mova         m1, m0
    mova         m3, m2
    punpcklbw    m0, m7
    punpckhbw    m1, m7
    punpcklbw    m2, m7
    punpckhbw    m3, m7
    psubw        m0, m2
    psubw        m1, m3
    mova  [r0+r4+0], m0
    mova  [r0+r4+8], m1
    add          r1, r3
    add          r2, r3
    add          r4, 16
    jne .loop
    REP_RET

INIT_MMX mmx
; int ff_pix_sum16_mmx(uint8_t *pix, int line_size)
cglobal pix_sum16, 2, 3
    movsxdifnidn r1, r1d
    mov          r2, r1
    neg          r2
    shl          r2, 4
    sub          r0, r2
    pxor         m7, m7
    pxor         m6, m6
.loop:
    mova         m0, [r0+r2+0]
    mova         m1, [r0+r2+0]
    mova         m2, [r0+r2+8]
    mova         m3, [r0+r2+8]
    punpcklbw    m0, m7
    punpckhbw    m1, m7
    punpcklbw    m2, m7
    punpckhbw    m3, m7
    paddw        m1, m0
    paddw        m3, m2
    paddw        m3, m1
    paddw        m6, m3
    add          r2, r1
    js .loop
    mova         m5, m6
    psrlq        m6, 32
    paddw        m6, m5
    mova         m5, m6
    psrlq        m6, 16
    paddw        m6, m5
    movd        eax, m6
    and         eax, 0xffff
    RET

INIT_MMX mmx
; int ff_pix_norm1_mmx(uint8_t *pix, int line_size)
cglobal pix_norm1, 2, 4
    movsxdifnidn r1, r1d
    mov          r2, 16
    pxor         m0, m0
    pxor         m7, m7
.loop:
    mova         m2, [r0+0]
    mova         m3, [r0+8]
    mova         m1, m2
    punpckhbw    m1, m0
    punpcklbw    m2, m0
    mova         m4, m3
    punpckhbw    m3, m0
    punpcklbw    m4, m0
    pmaddwd      m1, m1
    pmaddwd      m2, m2
    pmaddwd      m3, m3
    pmaddwd      m4, m4
    paddd        m2, m1
    paddd        m4, m3
    paddd        m7, m2
    add          r0, r1
    paddd        m7, m4
    dec r2
    jne .loop
    mova         m1, m7
    psrlq        m7, 32
    paddd        m1, m7
    movd        eax, m1
    RET

