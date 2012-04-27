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
 * @file
 * H.264 / AVC / MPEG4 part10 codec.
 * non-MMX i386-specific optimizations for H.264
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#ifndef AVCODEC_X86_H264_I386_H
#define AVCODEC_X86_H264_I386_H

#include <stddef.h>

#include "libavcodec/cabac.h"
#include "cabac.h"

//FIXME use some macros to avoid duplicating get_cabac (cannot be done yet
//as that would make optimization work hard)
#if HAVE_7REGS
static int decode_significance_x86(CABACContext *c, int max_coeff,
                                   uint8_t *significant_coeff_ctx_base,
                                   int *index, x86_reg last_off){
    void *end= significant_coeff_ctx_base + max_coeff - 1;
    int minusstart= -(intptr_t)significant_coeff_ctx_base;
    int minusindex= 4-(intptr_t)index;
    int bit;
    x86_reg coeff_count;

#ifdef BROKEN_RELOCATIONS
    void *tables;

    __asm__ volatile(
        "lea   "MANGLE(ff_h264_cabac_tables)", %0      \n\t"
        : "=&r"(tables)
    );
#endif

    __asm__ volatile(
        "3:                                     \n\t"

        BRANCHLESS_GET_CABAC("%4", "%q4", "(%1)", "%3", "%w3",
                             "%5", "%q5", "%k0", "%b0",
                             "%a11(%6)", "%a12(%6)", "%a13", "%a14", "%a15", "%16")

        "test $1, %4                            \n\t"
        " jz 4f                                 \n\t"
        "add  %10, %1                           \n\t"

        BRANCHLESS_GET_CABAC("%4", "%q4", "(%1)", "%3", "%w3",
                             "%5", "%q5", "%k0", "%b0",
                             "%a11(%6)", "%a12(%6)", "%a13", "%a14", "%a15", "%16")

        "sub  %10, %1                           \n\t"
        "mov  %2, %0                            \n\t"
        "movl %7, %%ecx                         \n\t"
        "add  %1, %%"REG_c"                     \n\t"
        "movl %%ecx, (%0)                       \n\t"

        "test $1, %4                            \n\t"
        " jnz 5f                                \n\t"

        "add"OPSIZE"  $4, %2                    \n\t"

        "4:                                     \n\t"
        "add  $1, %1                            \n\t"
        "cmp  %8, %1                            \n\t"
        " jb 3b                                 \n\t"
        "mov  %2, %0                            \n\t"
        "movl %7, %%ecx                         \n\t"
        "add  %1, %%"REG_c"                     \n\t"
        "movl %%ecx, (%0)                       \n\t"
        "5:                                     \n\t"
        "add  %9, %k0                           \n\t"
        "shr $2, %k0                            \n\t"
        : "=&q"(coeff_count), "+r"(significant_coeff_ctx_base), "+m"(index),
          "+&r"(c->low), "=&r"(bit), "+&r"(c->range)
        : "r"(c), "m"(minusstart), "m"(end), "m"(minusindex), "m"(last_off),
          "i"(offsetof(CABACContext, bytestream)),
          "i"(offsetof(CABACContext, bytestream_end)),
          "i"(H264_NORM_SHIFT_OFFSET),
          "i"(H264_LPS_RANGE_OFFSET),
          "i"(H264_MLPS_STATE_OFFSET) TABLES_ARG
        : "%"REG_c, "memory"
    );
    return coeff_count;
}

static int decode_significance_8x8_x86(CABACContext *c,
                                       uint8_t *significant_coeff_ctx_base,
                                       int *index, uint8_t *last_coeff_ctx_base, const uint8_t *sig_off){
    int minusindex= 4-(intptr_t)index;
    int bit;
    x86_reg coeff_count;
    x86_reg last=0;
    x86_reg state;

#ifdef BROKEN_RELOCATIONS
    void *tables;

    __asm__ volatile(
        "lea    "MANGLE(ff_h264_cabac_tables)", %0      \n\t"
        : "=&r"(tables)
    );
#endif

    __asm__ volatile(
        "mov %1, %6                             \n\t"
        "3:                                     \n\t"

        "mov %10, %0                            \n\t"
        "movzbl (%0, %6), %k6                   \n\t"
        "add %9, %6                             \n\t"

        BRANCHLESS_GET_CABAC("%4", "%q4", "(%6)", "%3", "%w3",
                             "%5", "%q5", "%k0", "%b0",
                             "%a12(%7)", "%a13(%7)", "%a14", "%a15", "%a16", "%18")

        "mov %1, %k6                            \n\t"
        "test $1, %4                            \n\t"
        " jz 4f                                 \n\t"

#ifdef BROKEN_RELOCATIONS
        "movzbl %a17(%18, %q6), %k6\n\t"
#else
        "movzbl "MANGLE(ff_h264_cabac_tables)"+%a17(%k6), %k6\n\t"
#endif
        "add %11, %6                            \n\t"

        BRANCHLESS_GET_CABAC("%4", "%q4", "(%6)", "%3", "%w3",
                             "%5", "%q5", "%k0", "%b0",
                             "%a12(%7)", "%a13(%7)", "%a14", "%a15", "%a16", "%18")

        "mov %2, %0                             \n\t"
        "mov %1, %k6                            \n\t"
        "movl %k6, (%0)                         \n\t"

        "test $1, %4                            \n\t"
        " jnz 5f                                \n\t"

        "add"OPSIZE"  $4, %2                    \n\t"

        "4:                                     \n\t"
        "addl $1, %k6                           \n\t"
        "mov %k6, %1                            \n\t"
        "cmpl $63, %k6                          \n\t"
        " jb 3b                                 \n\t"
        "mov %2, %0                             \n\t"
        "movl %k6, (%0)                         \n\t"
        "5:                                     \n\t"
        "addl %8, %k0                           \n\t"
        "shr $2, %k0                            \n\t"
        : "=&q"(coeff_count), "+m"(last), "+m"(index), "+&r"(c->low),
          "=&r"(bit), "+&r"(c->range), "=&r"(state)
        : "r"(c), "m"(minusindex), "m"(significant_coeff_ctx_base),
          "m"(sig_off), "m"(last_coeff_ctx_base),
          "i"(offsetof(CABACContext, bytestream)),
          "i"(offsetof(CABACContext, bytestream_end)),
          "i"(H264_NORM_SHIFT_OFFSET),
          "i"(H264_LPS_RANGE_OFFSET),
          "i"(H264_MLPS_STATE_OFFSET),
          "i"(H264_LAST_COEFF_FLAG_OFFSET_8x8_OFFSET) TABLES_ARG
        : "%"REG_c, "memory"
    );
    return coeff_count;
}
#endif /* HAVE_7REGS && !defined(BROKEN_RELOCATIONS) */

#endif /* AVCODEC_X86_H264_I386_H */
