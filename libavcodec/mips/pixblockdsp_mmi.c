/*
 * Loongson SIMD optimized pixblockdsp
 *
 * Copyright (c) 2015 Loongson Technology Corporation Limited
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#include "pixblockdsp_mips.h"

void ff_get_pixels_8_mmi(int16_t *av_restrict block, const uint8_t *pixels,
        ptrdiff_t line_size)
{
    __asm__ volatile (
        "move $8, $0                    \n\t"
        "xor $f0, $f0, $f0              \n\t"
        "1:                             \n\t"
        "gsldlc1 $f2, 7(%1)             \n\t"
        "gsldrc1 $f2, 0(%1)             \n\t"
        "punpcklbh $f4, $f2, $f0        \n\t"
        "punpckhbh $f6, $f2, $f0        \n\t"
        "gssdxc1 $f4, 0(%0, $8)         \n\t"
        "gssdxc1 $f6, 8(%0, $8)         \n\t"
        "daddiu $8, $8, 16              \n\t"
        "daddu %1, %1, %2               \n\t"
        "daddi %3, %3, -1               \n\t"
        "bnez %3, 1b                    \n\t"
        ::"r"((uint8_t *)block),"r"(pixels),"r"(line_size),"r"(8)
        : "$8","memory"
    );
}

void ff_diff_pixels_mmi(int16_t *av_restrict block, const uint8_t *src1,
        const uint8_t *src2, int stride)
{
    __asm__ volatile (
        "dli $2, 8                     \n\t"
        "xor $f14, $f14, $f14          \n\t"
        "1:                            \n\t"
        "gsldlc1 $f0, 7(%1)            \n\t"
        "gsldrc1 $f0, 0(%1)            \n\t"
        "or $f2, $f0, $f0              \n\t"
        "gsldlc1 $f4, 7(%2)            \n\t"
        "gsldrc1 $f4, 0(%2)            \n\t"
        "or $f6, $f4, $f4              \n\t"
        "punpcklbh $f0, $f0, $f14      \n\t"
        "punpckhbh $f2, $f2, $f14      \n\t"
        "punpcklbh $f4, $f4, $f14      \n\t"
        "punpckhbh $f6, $f6, $f14      \n\t"
        "psubh $f0, $f0, $f4           \n\t"
        "psubh $f2, $f2, $f6           \n\t"
        "gssdlc1 $f0, 7(%0)            \n\t"
        "gssdrc1 $f0, 0(%0)            \n\t"
        "gssdlc1 $f2, 15(%0)           \n\t"
        "gssdrc1 $f2, 8(%0)            \n\t"
        "daddi %0, %0, 16              \n\t"
        "daddu %1, %1, %3              \n\t"
        "daddu %2, %2, %3              \n\t"
        "daddi $2, $2, -1              \n\t"
        "bgtz $2, 1b                   \n\t"
        ::"r"(block),"r"(src1),"r"(src2),"r"(stride)
        : "$2","memory"
    );
}
