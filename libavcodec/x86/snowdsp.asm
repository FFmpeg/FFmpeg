;*
;* ASM optimized Snow DSP functions
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

cextern snow_inner_add_yblock_c

SECTION .text

%assign FRAC_BITS     4
%assign LOG2_OBMC_MAX 6

%macro ADD_YBLOCK_PROLOGUE 1
%assign stack_offset 0
%if ARCH_X86_32
    PROLOGUE 1, 7, 7+(%1>>4), obmc, offset, dst8, lines, b_h, src_x, dst
    ; copy all four block pointers to the stack to be able to load
    ; them via esp
    mov             r2, r2m
    mov           b_hd, b_hm
    mov         src_xd, src_xm
    movups          m1, [r2]
    mov         linesq, r7m
    shl         src_xd, 1              ; convert src_x from IDWTELEM to bytes
    mov          dst8q, r9m
    mov         src_xm, src_xd
    ; Just reuse the space for the arguments to store the block pointers.
%if HAVE_ALIGNED_STACK
    movaps         r0m, m1
%else
    movups         r0m, m1
%endif
    %define src_strideq r6m
%else ; X64
    PROLOGUE 1, 12, 7+(%1>>4), obmc, offset, dst8, lines, b_h, src_x, src_stride, dst, block0, block1, block2, block3
    mov        block0q, [r2q]
    mov        block1q, [r2q +   gprsize]
    mov        block2q, [r2q + 2*gprsize]
    mov        block3q, [r2q + 3*gprsize]
    movifnidn     b_hd, b_hm
    movifnidn   src_xd, src_xm
    shl         src_xd, 1              ; convert src_x from IDWTELEM to bytes and zero-extend it
    movsxd src_strideq, src_stridem
    mov         linesq, r7mp
    mov          dst8q, r9mp
%endif
    xor        offsetd, offsetd
    psllw           m0, FRAC_BITS - 1  ; pw_m8
%endmacro

%macro LOAD_BLOCKPOINTER_FOR_X86_32 2
%if ARCH_X86_32
    ; we put block #i into the spot of register r#i
    mov             r5, r %+ %1 %+ m
    mov             r6, r %+ %2 %+ m
    %xdefine block%1q r5
    %xdefine block%2q r6
%endif
%endmacro

INIT_XMM ssse3
; void ff_snow_inner_add_yblock_ssse3(const uint8_t *obmc, const int obmc_stride,
;                                     uint8_t **block, int b_w, int b_h, int src_x,
;                                     int src_stride, IDWTELEM *const *lines,
;                                     int add, uint8_t *dst8);
; Don't use cglobal to load args, as we may want to perform
; a tail call to ff_snow_inner_add_yblock.
cglobal snow_inner_add_yblock
    pcmpeqw         m0, m0
%if ARCH_X86_32
    mov            r0d, r3m     ; block width
    cmp            r0d, 16
    je            .w16
    cmp            r0d, 8
    jne snow_inner_add_yblock_c
    cmp           r1mp, 16
    jne snow_inner_add_yblock_c
%else
    ; all arguments used to check for support are already in registers
    cmp            r3d, 16
    je            .w16
    cmp            r3d, 8
    jne snow_inner_add_yblock_c
    cmp            r1d, 16
    jne snow_inner_add_yblock_c
%endif
    ADD_YBLOCK_PROLOGUE 8
    .loop8:
        LOAD_BLOCKPOINTER_FOR_X86_32 1, 3
        movq            m3, [block3q+offsetq]
        movq            m4, [block1q+offsetq]
        mova            m1, [obmcq]
        mova            m2, [obmcq+16*8]
%if ARCH_X86_64
        mov           dstq, [linesq]
%endif
        LOAD_BLOCKPOINTER_FOR_X86_32 0, 2
        movq            m5, [block2q+offsetq]
        movq            m6, [block0q+offsetq]
        punpcklbw       m3, m4
%if ARCH_X86_32
        mov           dstq, [linesq]
        add           dstq, src_xm
%endif
        SBUTTERFLY      bw, 1, 2, 4
%if ARCH_X86_64
        movu            m4, [dstq+src_xq]
%else
        movu            m4, [dstq]
%endif
        pmaddubsw       m3, m1
        add          obmcq, 16
        punpcklbw       m5, m6
        pmaddubsw       m5, m2
        add         linesq, gprsize
        paddw           m3, m5
        psubw           m4, m0         ; + 1<<(FRAC_BITS-1)
        psrlw           m3, LOG2_OBMC_MAX - FRAC_BITS
        paddw           m3, m4
        psraw           m3, FRAC_BITS
        packuswb        m3, m3
        movq [dst8q+offsetq], m3
        add        offsetq, src_strideq
        dec           b_hd
        jnz         .loop8
    RET
    .w16:
    ADD_YBLOCK_PROLOGUE 16
    .loop16:
        LOAD_BLOCKPOINTER_FOR_X86_32 2, 3
        mova            m3, [block3q+offsetq]
        mova            m4, [block2q+offsetq]
        mova            m1, [obmcq]
        mova            m2, [obmcq+16]
        LOAD_BLOCKPOINTER_FOR_X86_32 0, 1
        SBUTTERFLY      bw, 3, 4, 7
        mova            m5, [block1q+offsetq]
        mova            m6, [block0q+offsetq]
        SBUTTERFLY      bw, 1, 2, 7
        mov           dstq, [linesq]
        pmaddubsw       m3, m1
        mova            m1, [obmcq+32*16]
        pmaddubsw       m4, m2
        mova            m2, [obmcq+32*16+16]
%if ARCH_X86_32
        add           dstq, src_xm
%endif
        SBUTTERFLY      bw, 5, 6, 7
        SBUTTERFLY      bw, 1, 2, 7
        pmaddubsw       m5, m1
        add         linesq, gprsize
        pmaddubsw       m6, m2
        paddw           m3, m5
        paddw           m4, m6
        psrlw           m3, LOG2_OBMC_MAX - FRAC_BITS
        psrlw           m4, LOG2_OBMC_MAX - FRAC_BITS
        add          obmcq, 32
%if ARCH_X86_32
        paddw           m3, [dstq]
        paddw           m4, [dstq+16]
%else
        paddw           m3, [dstq+src_xq]
        paddw           m4, [dstq+src_xq+16]
%endif
        psubw           m3, m0         ; + 1<<(FRAC_BITS-1)
        psubw           m4, m0         ; + 1<<(FRAC_BITS-1)
        psraw           m3, FRAC_BITS
        psraw           m4, FRAC_BITS
        packuswb        m3, m4
        movu [dst8q+offsetq], m3
        add        offsetq, src_strideq
        dec           b_hd
        jnz        .loop16
    RET
