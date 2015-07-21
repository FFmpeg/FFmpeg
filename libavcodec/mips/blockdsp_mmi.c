/*
 * Loongson SIMD optimized blockdsp
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

#include "blockdsp_mips.h"

void ff_fill_block16_mmi(uint8_t *block, uint8_t value, int line_size, int h)
{
    __asm__ volatile (
        "move $8, %3                \r\n"
        "move $9, %0                \r\n"
        "dmtc1 %1, $f2              \r\n"
        "punpcklbh $f2, $f2, $f2    \r\n"
        "punpcklbh $f2, $f2, $f2    \r\n"
        "punpcklbh $f2, $f2, $f2    \r\n"
        "1:                         \r\n"
        "gssdlc1 $f2, 7($9)         \r\n"
        "gssdrc1 $f2, 0($9)         \r\n"
        "gssdlc1 $f2, 15($9)        \r\n"
        "gssdrc1 $f2, 8($9)         \r\n"
        "daddi $8, $8, -1           \r\n"
        "daddu $9, $9, %2           \r\n"
        "bnez $8, 1b                \r\n"
        ::"r"(block),"r"(value),"r"(line_size),"r"(h)
        : "$8","$9"
    );
}

void ff_fill_block8_mmi(uint8_t *block, uint8_t value, int line_size, int h)
{
    __asm__ volatile (
        "move $8, %3                \r\n"
        "move $9, %0                \r\n"
        "dmtc1 %1, $f2              \r\n"
        "punpcklbh $f2, $f2, $f2    \r\n"
        "punpcklbh $f2, $f2, $f2    \r\n"
        "punpcklbh $f2, $f2, $f2    \r\n"
        "1:                         \r\n"
        "gssdlc1 $f2, 7($9)         \r\n"
        "gssdrc1 $f2, 0($9)         \r\n"
        "daddi $8, $8, -1           \r\n"
        "daddu $9, $9, %2           \r\n"
        "bnez $8, 1b                \r\n"
        ::"r"(block),"r"(value),"r"(line_size),"r"(h)
        : "$8","$9"
    );
}

void ff_clear_block_mmi(int16_t *block)
{
    __asm__ volatile (
        "xor $f0, $f0, $f0              \r\n"
        "xor $f2, $f2, $f2              \r\n"
        "gssqc1 $f0, $f2,   0(%0)       \r\n"
        "gssqc1 $f0, $f2,  16(%0)       \r\n"
        "gssqc1 $f0, $f2,  32(%0)       \r\n"
        "gssqc1 $f0, $f2,  48(%0)       \r\n"
        "gssqc1 $f0, $f2,  64(%0)       \r\n"
        "gssqc1 $f0, $f2,  80(%0)       \r\n"
        "gssqc1 $f0, $f2,  96(%0)       \r\n"
        "gssqc1 $f0, $f2, 112(%0)       \r\n"
        ::"r"(block)
        : "memory"
    );
}

void ff_clear_blocks_mmi(int16_t *block)
{
    __asm__ volatile (
        "xor $f0, $f0, $f0              \r\n"
        "xor $f2, $f2, $f2              \r\n"
        "gssqc1 $f0, $f2,   0(%0)       \r\n"
        "gssqc1 $f0, $f2,  16(%0)       \r\n"
        "gssqc1 $f0, $f2,  32(%0)       \r\n"
        "gssqc1 $f0, $f2,  48(%0)       \r\n"
        "gssqc1 $f0, $f2,  64(%0)       \r\n"
        "gssqc1 $f0, $f2,  80(%0)       \r\n"
        "gssqc1 $f0, $f2,  96(%0)       \r\n"
        "gssqc1 $f0, $f2, 112(%0)       \r\n"

        "gssqc1 $f0, $f2, 128(%0)       \r\n"
        "gssqc1 $f0, $f2, 144(%0)       \r\n"
        "gssqc1 $f0, $f2, 160(%0)       \r\n"
        "gssqc1 $f0, $f2, 176(%0)       \r\n"
        "gssqc1 $f0, $f2, 192(%0)       \r\n"
        "gssqc1 $f0, $f2, 208(%0)       \r\n"
        "gssqc1 $f0, $f2, 224(%0)       \r\n"
        "gssqc1 $f0, $f2, 240(%0)       \r\n"

        "gssqc1 $f0, $f2, 256(%0)       \r\n"
        "gssqc1 $f0, $f2, 272(%0)       \r\n"
        "gssqc1 $f0, $f2, 288(%0)       \r\n"
        "gssqc1 $f0, $f2, 304(%0)       \r\n"
        "gssqc1 $f0, $f2, 320(%0)       \r\n"
        "gssqc1 $f0, $f2, 336(%0)       \r\n"
        "gssqc1 $f0, $f2, 352(%0)       \r\n"
        "gssqc1 $f0, $f2, 368(%0)       \r\n"

        "gssqc1 $f0, $f2, 384(%0)       \r\n"
        "gssqc1 $f0, $f2, 400(%0)       \r\n"
        "gssqc1 $f0, $f2, 416(%0)       \r\n"
        "gssqc1 $f0, $f2, 432(%0)       \r\n"
        "gssqc1 $f0, $f2, 448(%0)       \r\n"
        "gssqc1 $f0, $f2, 464(%0)       \r\n"
        "gssqc1 $f0, $f2, 480(%0)       \r\n"
        "gssqc1 $f0, $f2, 496(%0)       \r\n"

        "gssqc1 $f0, $f2, 512(%0)       \r\n"
        "gssqc1 $f0, $f2, 528(%0)       \r\n"
        "gssqc1 $f0, $f2, 544(%0)       \r\n"
        "gssqc1 $f0, $f2, 560(%0)       \r\n"
        "gssqc1 $f0, $f2, 576(%0)       \r\n"
        "gssqc1 $f0, $f2, 592(%0)       \r\n"
        "gssqc1 $f0, $f2, 608(%0)       \r\n"
        "gssqc1 $f0, $f2, 624(%0)       \r\n"

        "gssqc1 $f0, $f2, 640(%0)       \r\n"
        "gssqc1 $f0, $f2, 656(%0)       \r\n"
        "gssqc1 $f0, $f2, 672(%0)       \r\n"
        "gssqc1 $f0, $f2, 688(%0)       \r\n"
        "gssqc1 $f0, $f2, 704(%0)       \r\n"
        "gssqc1 $f0, $f2, 720(%0)       \r\n"
        "gssqc1 $f0, $f2, 736(%0)       \r\n"
        "gssqc1 $f0, $f2, 752(%0)       \r\n"
        ::"r"(block)
        : "memory"
    );
}
