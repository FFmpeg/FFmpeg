/*
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
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

#ifndef AVCODEC_ARM_DCA_H
#define AVCODEC_ARM_DCA_H

#include <stdint.h>

#include "config.h"
#include "libavcodec/dcadsp.h"
#include "libavcodec/mathops.h"

#if HAVE_ARMV6_INLINE && AV_GCC_VERSION_AT_LEAST(4,4)

#define decode_blockcodes decode_blockcodes
static inline int decode_blockcodes(int code1, int code2, int levels,
                                    int32_t *values)
{
    int32_t v0, v1, v2, v3, v4, v5;

    __asm__ ("smmul   %8,  %14, %18           \n"
             "smmul   %11, %15, %18           \n"
             "smlabb  %14, %8,  %17, %14      \n"
             "smlabb  %15, %11, %17, %15      \n"
             "smmul   %9,  %8,  %18           \n"
             "smmul   %12, %11, %18           \n"
             "sub     %14, %14, %16, lsr #1   \n"
             "sub     %15, %15, %16, lsr #1   \n"
             "smlabb  %8,  %9,  %17, %8       \n"
             "smlabb  %11, %12, %17, %11      \n"
             "smmul   %10, %9,  %18           \n"
             "smmul   %13, %12, %18           \n"
             "str     %14, %0                 \n"
             "str     %15, %4                 \n"
             "sub     %8,  %8,  %16, lsr #1   \n"
             "sub     %11, %11, %16, lsr #1   \n"
             "smlabb  %9,  %10, %17, %9       \n"
             "smlabb  %12, %13, %17, %12      \n"
             "smmul   %14, %10, %18           \n"
             "smmul   %15, %13, %18           \n"
             "str     %8,  %1                 \n"
             "str     %11, %5                 \n"
             "sub     %9,  %9,  %16, lsr #1   \n"
             "sub     %12, %12, %16, lsr #1   \n"
             "smlabb  %10, %14, %17, %10      \n"
             "smlabb  %13, %15, %17, %13      \n"
             "str     %9,  %2                 \n"
             "str     %12, %6                 \n"
             "sub     %10, %10, %16, lsr #1   \n"
             "sub     %13, %13, %16, lsr #1   \n"
             "str     %10, %3                 \n"
             "str     %13, %7                 \n"
             : "=m"(values[0]), "=m"(values[1]),
               "=m"(values[2]), "=m"(values[3]),
               "=m"(values[4]), "=m"(values[5]),
               "=m"(values[6]), "=m"(values[7]),
               "=&r"(v0), "=&r"(v1), "=&r"(v2),
               "=&r"(v3), "=&r"(v4), "=&r"(v5),
               "+&r"(code1), "+&r"(code2)
             : "r"(levels - 1), "r"(-levels), "r"(ff_inverse[levels]));

    return code1 | code2;
}

#endif

#endif /* AVCODEC_ARM_DCA_H */
