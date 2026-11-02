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
 * H.264 / AVC / MPEG-4 part10 codec.
 * non-SIMD x86-specific optimizations for H.264
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include <stddef.h>

#include "libavcodec/cabac.h"
#include "cabac.h"

#if HAVE_INLINE_ASM

#if ARCH_X86_64
#define REG64 "r"
#else
#define REG64 "m"
#endif

//FIXME use some macros to avoid duplicating get_cabac (cannot be done yet
//as that would make optimization work hard)
#if HAVE_X86_7REGS && !BROKEN_COMPILER
#define decode_significance decode_significance_x86
static int decode_significance_x86(CABACContext *c, int max_coeff,
                                   uint8_t *significant_coeff_ctx_base,
                                   int *index, x86_reg last_off){
    void *end= significant_coeff_ctx_base + max_coeff - 1;
    int minusstart= -(intptr_t)significant_coeff_ctx_base;
    int minusindex= 4-(intptr_t)index;
    int bit;
    x86_reg coeff_count;
    CABAC_TMP_DECL

#ifdef BROKEN_RELOCATIONS
    void *tables;

    __asm__ volatile(
        "lea   "MANGLE(ff_h264_cabac_tables)", %0      \n\t"
        : "=&r"(tables)
        : NAMED_CONSTRAINTS_ARRAY(ff_h264_cabac_tables)
    );
#endif

    __asm__ volatile(
        "3:                                     \n\t"

        BRANCHLESS_GET_CABAC("%[bit]", "%q[bit]", "(%[ctx])", "%[low]", "%w[low]",
                             "%[range]", "%q[range]", "%k[cnt]", "%b[cnt]",
                             "%[cabac_t1]", "%q[cabac_t1]", "%[cabac_t2]",
                             "%c[bs](%[c])", "%c[bsend](%[c])",
                             AV_STRINGIFY(H264_NORM_SHIFT_OFFSET),
                             AV_STRINGIFY(H264_LPS_RANGE_OFFSET),
                             AV_STRINGIFY(H264_MLPS_STATE_OFFSET),
                             "%[tables]")

        "test $1, %[bit]                        \n\t"
        " jz 4f                                 \n\t"
        "add  %[last_off], %[ctx]               \n\t"

        BRANCHLESS_GET_CABAC("%[bit]", "%q[bit]", "(%[ctx])", "%[low]", "%w[low]",
                             "%[range]", "%q[range]", "%k[cnt]", "%b[cnt]",
                             "%[cabac_t1]", "%q[cabac_t1]", "%[cabac_t2]",
                             "%c[bs](%[c])", "%c[bsend](%[c])",
                             AV_STRINGIFY(H264_NORM_SHIFT_OFFSET),
                             AV_STRINGIFY(H264_LPS_RANGE_OFFSET),
                             AV_STRINGIFY(H264_MLPS_STATE_OFFSET),
                             "%[tables]")

        "sub  %[last_off], %[ctx]               \n\t"
        "mov  %[index], %[cnt]                  \n\t"
        "movl %[minusstart], %%ecx              \n\t"
        "add  %[ctx], %%"FF_REG_c"              \n\t"
        "movl %%ecx, (%[cnt])                   \n\t"

        "test $1, %[bit]                        \n\t"
        " jnz 5f                                \n\t"

        "add"FF_OPSIZE"  $4, %[index]           \n\t"

        "4:                                     \n\t"
        "add  $1, %[ctx]                        \n\t"
        "cmp  %[end], %[ctx]                    \n\t"
        " jb 3b                                 \n\t"
        "mov  %[index], %[cnt]                  \n\t"
        "movl %[minusstart], %%ecx              \n\t"
        "add  %[ctx], %%"FF_REG_c"              \n\t"
        "movl %%ecx, (%[cnt])                   \n\t"
        "5:                                     \n\t"
        "add  %[minusindex], %k[cnt]            \n\t"
        "shr $2, %k[cnt]                        \n\t"
        : [cnt]"=&q"(coeff_count), [ctx]"+r"(significant_coeff_ctx_base), [index]"+m"(index),
          [low]"+&r"(c->low), [bit]"=&r"(bit), [range]"+&r"(c->range)
          CABAC_TMP_OPERANDS
        : [c]"r"(c), [minusstart]"m"(minusstart), [end]"m"(end), [minusindex]"m"(minusindex), [last_off]"m"(last_off),
          [bs]"i"(offsetof(CABACContext, bytestream)),
          [bsend]"i"(offsetof(CABACContext, bytestream_end))
          TABLES_ARG
        : "%"FF_REG_c, "memory"
    );
    return coeff_count;
}

#define decode_significance_8x8 decode_significance_8x8_x86
static int decode_significance_8x8_x86(CABACContext *c,
                                       uint8_t *significant_coeff_ctx_base,
                                       int *index, uint8_t *last_coeff_ctx_base, const uint8_t *sig_off){
    int minusindex= 4-(intptr_t)index;
    int bit;
    x86_reg coeff_count;
    x86_reg last=0;
    x86_reg state;
    CABAC_TMP_DECL

#ifdef BROKEN_RELOCATIONS
    void *tables;

    __asm__ volatile(
        "lea    "MANGLE(ff_h264_cabac_tables)", %0      \n\t"
        : "=&r"(tables)
        : NAMED_CONSTRAINTS_ARRAY(ff_h264_cabac_tables)
    );
#endif

    __asm__ volatile(
        "mov %[last], %[state]                  \n\t"
        "3:                                     \n\t"

        "mov %[sig_off], %[cnt]                 \n\t"
        "movzb (%[cnt], %[state]), %[state]     \n\t"
        "add %[sig_base], %[state]              \n\t"

        BRANCHLESS_GET_CABAC("%[bit]", "%q[bit]", "(%[state])", "%[low]", "%w[low]",
                             "%[range]", "%q[range]", "%k[cnt]", "%b[cnt]",
                             "%[cabac_t1]", "%q[cabac_t1]", "%[cabac_t2]",
                             "%c[bs](%[c])", "%c[bsend](%[c])",
                             AV_STRINGIFY(H264_NORM_SHIFT_OFFSET),
                             AV_STRINGIFY(H264_LPS_RANGE_OFFSET),
                             AV_STRINGIFY(H264_MLPS_STATE_OFFSET),
                             "%[tables]")

        "mov %[last], %[state]                  \n\t"
        "test $1, %[bit]                        \n\t"
        " jz 4f                                 \n\t"

#ifdef BROKEN_RELOCATIONS
        "movzb %c[lastoff](%[tables], %q[state]), %[state]\n\t"
#else
        "movzb "MANGLE(ff_h264_cabac_tables)"+%c[lastoff](%[state]), %[state]\n\t"
#endif
        "add %[last_base], %[state]             \n\t"

        BRANCHLESS_GET_CABAC("%[bit]", "%q[bit]", "(%[state])", "%[low]", "%w[low]",
                             "%[range]", "%q[range]", "%k[cnt]", "%b[cnt]",
                             "%[cabac_t1]", "%q[cabac_t1]", "%[cabac_t2]",
                             "%c[bs](%[c])", "%c[bsend](%[c])",
                             AV_STRINGIFY(H264_NORM_SHIFT_OFFSET),
                             AV_STRINGIFY(H264_LPS_RANGE_OFFSET),
                             AV_STRINGIFY(H264_MLPS_STATE_OFFSET),
                             "%[tables]")

        "mov %[index], %[cnt]                   \n\t"
        "mov %[last], %[state]                  \n\t"
        "mov %k[state], (%[cnt])                \n\t"

        "test $1, %[bit]                        \n\t"
        " jnz 5f                                \n\t"

        "add"FF_OPSIZE"  $4, %[index]           \n\t"

        "4:                                     \n\t"
        "add $1, %[state]                       \n\t"
        "mov %[state], %[last]                  \n\t"
        "cmp $63, %[state]                      \n\t"
        " jb 3b                                 \n\t"
        "mov %[index], %[cnt]                   \n\t"
        "mov %k[state], (%[cnt])                \n\t"
        "5:                                     \n\t"
        "addl %[minusindex], %k[cnt]            \n\t"
        "shr $2, %k[cnt]                        \n\t"
        : [cnt]"=&q"(coeff_count), [last]"+"REG64(last), [index]"+"REG64(index), [low]"+&r"(c->low),
          [bit]"=&r"(bit), [range]"+&r"(c->range), [state]"=&r"(state)
          CABAC_TMP_OPERANDS
        : [c]"r"(c), [minusindex]"m"(minusindex), [sig_base]"m"(significant_coeff_ctx_base),
          [sig_off]REG64(sig_off), [last_base]REG64(last_coeff_ctx_base),
          [bs]"i"(offsetof(CABACContext, bytestream)),
          [bsend]"i"(offsetof(CABACContext, bytestream_end)),
          [lastoff]"i"(H264_LAST_COEFF_FLAG_OFFSET_8x8_OFFSET)
          TABLES_ARG
        : "%"FF_REG_c, "memory"
    );
    return coeff_count;
}
#endif /* HAVE_X86_7REGS && BROKEN_COMPILER */

#endif /* HAVE_INLINE_ASM */
