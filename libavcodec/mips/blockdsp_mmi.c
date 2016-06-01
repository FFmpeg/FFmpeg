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
#include "libavutil/mips/asmdefs.h"

void ff_fill_block16_mmi(uint8_t *block, uint8_t value, int line_size, int h)
{
    double ftmp[1];

    __asm__ volatile (
        "mtc1       %[value],   %[ftmp0]                                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "1:                                                             \n\t"
        "gssdlc1    %[ftmp0],   0x07(%[block])                          \n\t"
        "gssdrc1    %[ftmp0],   0x00(%[block])                          \n\t"
        PTR_ADDI    "%[h],      %[h],           -0x01                   \n\t"
        "gssdlc1    %[ftmp0],   0x0f(%[block])                          \n\t"
        "gssdrc1    %[ftmp0],   0x08(%[block])                          \n\t"
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [block]"+&r"(block),              [h]"+&r"(h),
          [ftmp0]"=&f"(ftmp[0])
        : [value]"r"(value),                [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}

void ff_fill_block8_mmi(uint8_t *block, uint8_t value, int line_size, int h)
{
    double ftmp0;

    __asm__ volatile (
        "mtc1       %[value],   %[ftmp0]                                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "1:                                                             \n\t"
        "gssdlc1    %[ftmp0],   0x07(%[block])                          \n\t"
        "gssdrc1    %[ftmp0],   0x00(%[block])                          \n\t"
        PTR_ADDI   "%[h],       %[h],           -0x01                   \n\t"
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [block]"+&r"(block),              [h]"+&r"(h),
          [ftmp0]"=&f"(ftmp0)
        : [value]"r"(value),                [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}

void ff_clear_block_mmi(int16_t *block)
{
    double ftmp[2];

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp1]                \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x00(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x10(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x20(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x30(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x40(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x50(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x60(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x70(%[block])          \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1])
        : [block]"r"(block)
        : "memory"
    );
}

void ff_clear_blocks_mmi(int16_t *block)
{
    double ftmp[2];

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp1]                \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x00(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x10(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x20(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x30(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x40(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x50(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x60(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x70(%[block])          \n\t"

        "gssqc1     %[ftmp0],   %[ftmp1],       0x80(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x90(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0xa0(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0xb0(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0xc0(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0xd0(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0xe0(%[block])          \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0xf0(%[block])          \n\t"

        "gssqc1     %[ftmp0],   %[ftmp1],       0x100(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x110(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x120(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x130(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x140(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x150(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x160(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x170(%[block])         \n\t"

        "gssqc1     %[ftmp0],   %[ftmp1],       0x180(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x190(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x1a0(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x1b0(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x1c0(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x1d0(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x1e0(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x1f0(%[block])         \n\t"

        "gssqc1     %[ftmp0],   %[ftmp1],       0x200(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x210(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x220(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x230(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x240(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x250(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x260(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x270(%[block])         \n\t"

        "gssqc1     %[ftmp0],   %[ftmp1],       0x280(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x290(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x2a0(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x2b0(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x2c0(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x2d0(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x2e0(%[block])         \n\t"
        "gssqc1     %[ftmp0],   %[ftmp1],       0x2f0(%[block])         \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1])
        : [block]"r"((mips_reg)block)
        : "memory"
    );
}
