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
#include "libavutil/mips/mmiutils.h"

void ff_fill_block16_mmi(uint8_t *block, uint8_t value, ptrdiff_t line_size, int h)
{
    double ftmp[1];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "mtc1       %[value],   %[ftmp0]                                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "1:                                                             \n\t"
        MMI_SDC1(%[ftmp0], %[block], 0x00)
        PTR_ADDI   "%[h],       %[h],           -0x01                   \n\t"
        MMI_SDC1(%[ftmp0], %[block], 0x08)
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),
          RESTRICT_ASM_ALL64
          [block]"+&r"(block),              [h]"+&r"(h)
        : [value]"r"(value),                [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}

void ff_fill_block8_mmi(uint8_t *block, uint8_t value, ptrdiff_t line_size, int h)
{
    double ftmp0;
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "mtc1       %[value],   %[ftmp0]                                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "1:                                                             \n\t"
        MMI_SDC1(%[ftmp0], %[block], 0x00)
        PTR_ADDI   "%[h],       %[h],           -0x01                   \n\t"
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp0),
          RESTRICT_ASM_ALL64
          [block]"+&r"(block),              [h]"+&r"(h)
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
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x00)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x10)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x20)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x30)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x40)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x50)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x60)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x70)
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
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x00)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x10)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x20)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x30)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x40)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x50)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x60)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x70)

        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x80)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x90)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0xa0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0xb0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0xc0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0xd0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0xe0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0xf0)

        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x100)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x110)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x120)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x130)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x140)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x150)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x160)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x170)

        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x180)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x190)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x1a0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x1b0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x1c0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x1d0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x1e0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x1f0)

        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x200)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x210)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x220)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x230)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x240)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x250)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x260)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x270)

        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x280)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x290)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x2a0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x2b0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x2c0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x2d0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x2e0)
        MMI_SQC1(%[ftmp0], %[ftmp1], %[block], 0x2f0)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1])
        : [block]"r"((uint64_t *)block)
        : "memory"
    );
}
