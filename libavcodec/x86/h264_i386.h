/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file libavcodec/x86/h264_i386.h
 * H.264 / AVC / MPEG4 part10 codec.
 * non-MMX i386-specific optimizations for H.264
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef AVCODEC_X86_H264_I386_H
#define AVCODEC_X86_H264_I386_H

#include "libavcodec/cabac.h"

//FIXME use some macros to avoid duplicating get_cabac (cannot be done yet
//as that would make optimization work hard)
#if ARCH_X86 && HAVE_7REGS && HAVE_EBX_AVAILABLE && !defined(BROKEN_RELOCATIONS)
static int decode_significance_x86(CABACContext *c, int max_coeff,
                                   uint8_t *significant_coeff_ctx_base,
                                   int *index){
    void *end= significant_coeff_ctx_base + max_coeff - 1;
    int minusstart= -(int)significant_coeff_ctx_base;
    int minusindex= 4-(int)index;
    int coeff_count;
    __asm__ volatile(
        "movl "RANGE    "(%3), %%esi            \n\t"
        "movl "LOW      "(%3), %%ebx            \n\t"

        "2:                                     \n\t"

        BRANCHLESS_GET_CABAC("%%edx", "%3", "(%1)", "%%ebx",
                             "%%bx", "%%esi", "%%eax", "%%al")

        "test $1, %%edx                         \n\t"
        " jz 3f                                 \n\t"

        BRANCHLESS_GET_CABAC("%%edx", "%3", "61(%1)", "%%ebx",
                             "%%bx", "%%esi", "%%eax", "%%al")

        "mov  %2, %%"REG_a"                     \n\t"
        "movl %4, %%ecx                         \n\t"
        "add  %1, %%"REG_c"                     \n\t"
        "movl %%ecx, (%%"REG_a")                \n\t"

        "test $1, %%edx                         \n\t"
        " jnz 4f                                \n\t"

        "add  $4, %%"REG_a"                     \n\t"
        "mov  %%"REG_a", %2                     \n\t"

        "3:                                     \n\t"
        "add  $1, %1                            \n\t"
        "cmp  %5, %1                            \n\t"
        " jb 2b                                 \n\t"
        "mov  %2, %%"REG_a"                     \n\t"
        "movl %4, %%ecx                         \n\t"
        "add  %1, %%"REG_c"                     \n\t"
        "movl %%ecx, (%%"REG_a")                \n\t"
        "4:                                     \n\t"
        "add  %6, %%eax                         \n\t"
        "shr $2, %%eax                          \n\t"

        "movl %%esi, "RANGE    "(%3)            \n\t"
        "movl %%ebx, "LOW      "(%3)            \n\t"
        :"=&a"(coeff_count), "+r"(significant_coeff_ctx_base), "+m"(index)
        :"r"(c), "m"(minusstart), "m"(end), "m"(minusindex)
        : "%"REG_c, "%ebx", "%edx", "%esi", "memory"
    );
    return coeff_count;
}

static int decode_significance_8x8_x86(CABACContext *c,
                                       uint8_t *significant_coeff_ctx_base,
                                       int *index, const uint8_t *sig_off){
    int minusindex= 4-(int)index;
    int coeff_count;
    x86_reg last=0;
    __asm__ volatile(
        "movl "RANGE    "(%3), %%esi            \n\t"
        "movl "LOW      "(%3), %%ebx            \n\t"

        "mov %1, %%"REG_D"                      \n\t"
        "2:                                     \n\t"

        "mov %6, %%"REG_a"                      \n\t"
        "movzbl (%%"REG_a", %%"REG_D"), %%edi   \n\t"
        "add %5, %%"REG_D"                      \n\t"

        BRANCHLESS_GET_CABAC("%%edx", "%3", "(%%"REG_D")", "%%ebx",
                             "%%bx", "%%esi", "%%eax", "%%al")

        "mov %1, %%edi                          \n\t"
        "test $1, %%edx                         \n\t"
        " jz 3f                                 \n\t"

        "movzbl "MANGLE(last_coeff_flag_offset_8x8)"(%%edi), %%edi\n\t"
        "add %5, %%"REG_D"                      \n\t"

        BRANCHLESS_GET_CABAC("%%edx", "%3", "15(%%"REG_D")", "%%ebx",
                             "%%bx", "%%esi", "%%eax", "%%al")

        "mov %2, %%"REG_a"                      \n\t"
        "mov %1, %%edi                          \n\t"
        "movl %%edi, (%%"REG_a")                \n\t"

        "test $1, %%edx                         \n\t"
        " jnz 4f                                \n\t"

        "add $4, %%"REG_a"                      \n\t"
        "mov %%"REG_a", %2                      \n\t"

        "3:                                     \n\t"
        "addl $1, %%edi                         \n\t"
        "mov %%edi, %1                          \n\t"
        "cmpl $63, %%edi                        \n\t"
        " jb 2b                                 \n\t"
        "mov %2, %%"REG_a"                      \n\t"
        "movl %%edi, (%%"REG_a")                \n\t"
        "4:                                     \n\t"
        "addl %4, %%eax                         \n\t"
        "shr $2, %%eax                          \n\t"

        "movl %%esi, "RANGE    "(%3)            \n\t"
        "movl %%ebx, "LOW      "(%3)            \n\t"
        :"=&a"(coeff_count),"+m"(last), "+m"(index)
        :"r"(c), "m"(minusindex), "m"(significant_coeff_ctx_base), "m"(sig_off)
        : "%"REG_c, "%ebx", "%edx", "%esi", "%"REG_D, "memory"
    );
    return coeff_count;
}
#endif /* ARCH_X86 && HAVE_7REGS && HAVE_EBX_AVAILABLE */
       /* !defined(BROKEN_RELOCATIONS) */

#endif /* AVCODEC_X86_H264_I386_H */
