/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * H.264 / AVC / MPEG4 part10 codec.
 * non-MMX i386-specific optimizations for H.264
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef AVCODEC_X86_H264_I386_H
#define AVCODEC_X86_H264_I386_H

#include <stddef.h>

#include "libavcodec/cabac.h"

//FIXME use some macros to avoid duplicating get_cabac (cannot be done yet
//as that would make optimization work hard)
#if ARCH_X86 && HAVE_7REGS && !defined(BROKEN_RELOCATIONS)
static int decode_significance_x86(CABACContext *c, int max_coeff,
                                   uint8_t *significant_coeff_ctx_base,
                                   int *index, x86_reg last_off){
    void *end= significant_coeff_ctx_base + max_coeff - 1;
    int minusstart= -(int)significant_coeff_ctx_base;
    int minusindex= 4-(int)index;
    x86_reg coeff_count;
    int low;
    __asm__ volatile(
        "movl %a9(%4), %%esi                    \n\t"
        "movl %a10(%4), %3                      \n\t"

        "2:                                     \n\t"

        BRANCHLESS_GET_CABAC("%%edx", "%4", "(%1)", "%3",
                             "%w3", "%%esi", "%k0", "%b0", "%a11")

        "test $1, %%edx                         \n\t"
        " jz 3f                                 \n\t"
        "add  %8, %1                            \n\t"

        BRANCHLESS_GET_CABAC("%%edx", "%4", "(%1)", "%3",
                             "%w3", "%%esi", "%k0", "%b0", "%a11")

        "sub  %8, %1                            \n\t"
        "mov  %2, %0                            \n\t"
        "movl %5, %%ecx                         \n\t"
        "add  %1, %%"REG_c"                     \n\t"
        "movl %%ecx, (%0)                       \n\t"

        "test $1, %%edx                         \n\t"
        " jnz 4f                                \n\t"

        "add  $4, %0                            \n\t"
        "mov  %0, %2                            \n\t"

        "3:                                     \n\t"
        "add  $1, %1                            \n\t"
        "cmp  %6, %1                            \n\t"
        " jb 2b                                 \n\t"
        "mov  %2, %0                            \n\t"
        "movl %5, %%ecx                         \n\t"
        "add  %1, %%"REG_c"                     \n\t"
        "movl %%ecx, (%0)                       \n\t"
        "4:                                     \n\t"
        "add  %7, %k0                           \n\t"
        "shr $2, %k0                            \n\t"

        "movl %%esi, %a9(%4)                    \n\t"
        "movl %3, %a10(%4)                      \n\t"
        :"=&r"(coeff_count), "+r"(significant_coeff_ctx_base), "+m"(index),
         "=&r"(low)
        :"r"(c), "m"(minusstart), "m"(end), "m"(minusindex), "m"(last_off),
         "i"(offsetof(CABACContext, range)), "i"(offsetof(CABACContext, low)),
         "i"(offsetof(CABACContext, bytestream))
        : "%"REG_c, "%edx", "%esi", "memory"
    );
    return coeff_count;
}

static int decode_significance_8x8_x86(CABACContext *c,
                                       uint8_t *significant_coeff_ctx_base,
                                       int *index, x86_reg last_off, const uint8_t *sig_off){
    int minusindex= 4-(int)index;
    x86_reg coeff_count;
    int low;
    x86_reg last=0;
    __asm__ volatile(
        "movl %a9(%4), %%esi                    \n\t"
        "movl %a10(%4), %3                      \n\t"

        "mov %1, %%"REG_D"                      \n\t"
        "2:                                     \n\t"

        "mov %7, %0                             \n\t"
        "movzbl (%0, %%"REG_D"), %%edi          \n\t"
        "add %6, %%"REG_D"                      \n\t"

        BRANCHLESS_GET_CABAC("%%edx", "%4", "(%%"REG_D")", "%3",
                             "%w3", "%%esi", "%k0", "%b0", "%a11")

        "mov %1, %%edi                          \n\t"
        "test $1, %%edx                         \n\t"
        " jz 3f                                 \n\t"

        "movzbl "MANGLE(last_coeff_flag_offset_8x8)"(%%edi), %%edi\n\t"
        "add %6, %%"REG_D"                      \n\t"
        "add %8, %%"REG_D"                      \n\t"

        BRANCHLESS_GET_CABAC("%%edx", "%4", "(%%"REG_D")", "%3",
                             "%w3", "%%esi", "%k0", "%b0", "%a11")

        "mov %2, %0                             \n\t"
        "mov %1, %%edi                          \n\t"
        "movl %%edi, (%0)                       \n\t"

        "test $1, %%edx                         \n\t"
        " jnz 4f                                \n\t"

        "add $4, %0                             \n\t"
        "mov %0, %2                             \n\t"

        "3:                                     \n\t"
        "addl $1, %%edi                         \n\t"
        "mov %%edi, %1                          \n\t"
        "cmpl $63, %%edi                        \n\t"
        " jb 2b                                 \n\t"
        "mov %2, %0                             \n\t"
        "movl %%edi, (%0)                       \n\t"
        "4:                                     \n\t"
        "addl %5, %k0                           \n\t"
        "shr $2, %k0                            \n\t"

        "movl %%esi, %a9(%4)                    \n\t"
        "movl %3, %a10(%4)                      \n\t"
        :"=&r"(coeff_count),"+m"(last), "+m"(index), "=&r"(low)
        :"r"(c), "m"(minusindex), "m"(significant_coeff_ctx_base), "m"(sig_off), "m"(last_off),
         "i"(offsetof(CABACContext, range)), "i"(offsetof(CABACContext, low)),
         "i"(offsetof(CABACContext, bytestream))
        : "%"REG_c, "%edx", "%esi", "%"REG_D, "memory"
    );
    return coeff_count;
}
#endif /* ARCH_X86 && HAVE_7REGS && !defined(BROKEN_RELOCATIONS) */

#endif /* AVCODEC_X86_H264_I386_H */
