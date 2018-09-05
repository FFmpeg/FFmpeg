/*
 * Loongson SIMD optimized qpeldsp
 *
 * Copyright (c) 2016 Loongson Technology Corporation Limited
 * Copyright (c) 2016 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#include "hpeldsp_mips.h"
#include "libavcodec/bit_depth_template.c"
#include "libavutil/mips/mmiutils.h"
#include "constants.h"

void ff_put_pixels4_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    double ftmp[4];
    DECLARE_VAR_LOW32;

    __asm__ volatile (
        "1:                                                             \n\t"
        MMI_ULWC1(%[ftmp0], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],   %[pixels],      %[line_size]           \n\t"
        MMI_ULWC1(%[ftmp1], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],   %[pixels],      %[line_size]           \n\t"
        MMI_ULWC1(%[ftmp2], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],   %[pixels],      %[line_size]           \n\t"
        MMI_ULWC1(%[ftmp3], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],   %[pixels],      %[line_size]           \n\t"

        PTR_ADDI   "%[h],       %[h],           -0x04                   \n\t"

        MMI_SWC1(%[ftmp0], %[block], 0x00)
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"
        MMI_SWC1(%[ftmp1], %[block], 0x00)
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"
        MMI_SWC1(%[ftmp2], %[block], 0x00)
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"
        MMI_SWC1(%[ftmp3], %[block], 0x00)
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"

        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          RESTRICT_ASM_LOW32
          [block]"+&r"(block),              [pixels]"+&r"(pixels),
          [h]"+&r"(h)
        : [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}

void ff_put_pixels8_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    double ftmp[4];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "1:                                                             \n\t"
        MMI_ULDC1(%[ftmp0], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],   %[pixels],      %[line_size]           \n\t"
        MMI_ULDC1(%[ftmp1], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],   %[pixels],      %[line_size]           \n\t"
        MMI_ULDC1(%[ftmp2], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],   %[pixels],      %[line_size]           \n\t"
        MMI_ULDC1(%[ftmp3], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],   %[pixels],      %[line_size]           \n\t"

        PTR_ADDI   "%[h],       %[h],           -0x04                   \n\t"

        MMI_SDC1(%[ftmp0], %[block], 0x00)
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"
        MMI_SDC1(%[ftmp1], %[block], 0x00)
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"
        MMI_SDC1(%[ftmp2], %[block], 0x00)
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"
        MMI_SDC1(%[ftmp3], %[block], 0x00)
        PTR_ADDU   "%[block],   %[block],       %[line_size]            \n\t"

        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          RESTRICT_ASM_ALL64
          [block]"+&r"(block),              [pixels]"+&r"(pixels),
          [h]"+&r"(h)
        : [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}

void ff_put_pixels16_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    double ftmp[8];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "1:                                                            \n\t"
        MMI_ULDC1(%[ftmp0], %[pixels], 0x00)
        MMI_ULDC1(%[ftmp2], %[pixels], 0x08)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]           \n\t"
        MMI_ULDC1(%[ftmp1], %[pixels], 0x00)
        MMI_ULDC1(%[ftmp3], %[pixels], 0x08)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]           \n\t"
        MMI_ULDC1(%[ftmp4], %[pixels], 0x00)
        MMI_ULDC1(%[ftmp6], %[pixels], 0x08)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]           \n\t"
        MMI_ULDC1(%[ftmp5], %[pixels], 0x00)
        MMI_ULDC1(%[ftmp7], %[pixels], 0x08)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]           \n\t"

        PTR_ADDI   "%[h],       %[h],           -0x04                  \n\t"

        MMI_SDC1(%[ftmp0], %[block], 0x00)
        MMI_SDC1(%[ftmp2], %[block], 0x08)
        PTR_ADDU   "%[block],   %[block],       %[line_size]           \n\t"
        MMI_SDC1(%[ftmp1], %[block], 0x00)
        MMI_SDC1(%[ftmp3], %[block], 0x08)
        PTR_ADDU   "%[block],   %[block],       %[line_size]           \n\t"
        MMI_SDC1(%[ftmp4], %[block], 0x00)
        MMI_SDC1(%[ftmp6], %[block], 0x08)
        PTR_ADDU   "%[block],   %[block],       %[line_size]           \n\t"
        MMI_SDC1(%[ftmp5], %[block], 0x00)
        MMI_SDC1(%[ftmp7], %[block], 0x08)
        PTR_ADDU   "%[block],   %[block],       %[line_size]           \n\t"

        "bnez       %[h],       1b                                     \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          RESTRICT_ASM_ALL64
          [block]"+&r"(block),              [pixels]"+&r"(pixels),
          [h]"+&r"(h)
        : [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}

void ff_avg_pixels4_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    double ftmp[4];
    mips_reg addr[3];
    DECLARE_VAR_LOW32;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        PTR_ADDU   "%[addr2],   %[line_size],   %[line_size]            \n\t"
        "1:                                                             \n\t"
        PTR_ADDU   "%[addr0],   %[pixels],      %[line_size]            \n\t"
        MMI_ULWC1(%[ftmp0], %[pixels], 0x00)
        MMI_ULWC1(%[ftmp1], %[addr0], 0x00)
        PTR_ADDU   "%[addr1],   %[block],       %[line_size]            \n\t"
        MMI_ULWC1(%[ftmp2], %[block], 0x00)
        MMI_ULWC1(%[ftmp3], %[addr1], 0x00)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        MMI_SWC1(%[ftmp0], %[block], 0x00)
        MMI_SWXC1(%[ftmp1], %[block], %[line_size], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],      %[addr2]                \n\t"
        PTR_ADDU   "%[block],   %[block],       %[addr2]                \n\t"

        PTR_ADDU   "%[addr0],   %[pixels],      %[line_size]            \n\t"
        MMI_ULWC1(%[ftmp0], %[pixels], 0x00)
        MMI_ULWC1(%[ftmp1], %[addr0], 0x00)
        PTR_ADDU   "%[addr1],   %[block],       %[line_size]            \n\t"
        MMI_ULWC1(%[ftmp2], %[block], 0x00)
        MMI_ULWC1(%[ftmp3], %[addr1], 0x00)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        MMI_SWC1(%[ftmp0], %[block], 0x00)
        MMI_SWXC1(%[ftmp1], %[block], %[line_size], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],      %[addr2]                \n\t"
        PTR_ADDU   "%[block],   %[block],       %[addr2]                \n\t"

        PTR_ADDI   "%[h],       %[h],           -0x04                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          RESTRICT_ASM_LOW32
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),
          [block]"+&r"(block),              [pixels]"+&r"(pixels),
          [h]"+&r"(h)
        : [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}

void ff_avg_pixels8_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    double ftmp[4];
    mips_reg addr[3];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        PTR_ADDU   "%[addr2],   %[line_size],   %[line_size]            \n\t"
        "1:                                                             \n\t"
        MMI_ULDC1(%[ftmp0], %[pixels], 0x00)
        PTR_ADDU   "%[addr0],   %[pixels],      %[line_size]            \n\t"
        MMI_ULDC1(%[ftmp1], %[addr0], 0x00)
        PTR_ADDU   "%[addr1],   %[block],       %[line_size]            \n\t"
        MMI_ULDC1(%[ftmp2], %[block], 0x00)
        MMI_ULDC1(%[ftmp3], %[addr1], 0x00)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        MMI_SDC1(%[ftmp0], %[block], 0x00)
        MMI_SDXC1(%[ftmp1], %[block], %[line_size], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],      %[addr2]                \n\t"
        PTR_ADDU   "%[block],   %[block],       %[addr2]                \n\t"

        MMI_ULDC1(%[ftmp0], %[pixels], 0x00)
        PTR_ADDU   "%[addr0],   %[pixels],      %[line_size]            \n\t"
        MMI_ULDC1(%[ftmp1], %[addr0], 0x00)
        PTR_ADDU   "%[addr1],   %[block],       %[line_size]            \n\t"
        MMI_ULDC1(%[ftmp2], %[block], 0x00)
        MMI_ULDC1(%[ftmp3], %[addr1], 0x00)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        MMI_SDC1(%[ftmp0], %[block], 0x00)
        MMI_SDXC1(%[ftmp1], %[block], %[line_size], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],      %[addr2]                \n\t"
        PTR_ADDU   "%[block],   %[block],       %[addr2]                \n\t"

        PTR_ADDI   "%[h],       %[h],           -0x04                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),
          [block]"+&r"(block),              [pixels]"+&r"(pixels),
          [h]"+&r"(h)
        : [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}

void ff_avg_pixels16_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    double ftmp[8];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "1:                                                             \n\t"
        PTR_ADDI   "%[h],       %[h],           -0x04                   \n\t"
        MMI_ULDC1(%[ftmp0], %[pixels], 0x00)
        MMI_ULDC1(%[ftmp4], %[pixels], 0x08)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        MMI_ULDC1(%[ftmp1], %[pixels], 0x00)
        MMI_ULDC1(%[ftmp5], %[pixels], 0x08)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        MMI_ULDC1(%[ftmp2], %[block], 0x00)
        MMI_ULDC1(%[ftmp6], %[block], 0x08)
        PTR_ADDU   "%[addr0],   %[block],       %[line_size]            \n\t"
        MMI_ULDC1(%[ftmp3], %[addr0], 0x00)
        MMI_ULDC1(%[ftmp7], %[addr0], 0x08)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "pavgb      %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        MMI_SDC1(%[ftmp0], %[block], 0x00)
        MMI_SDC1(%[ftmp4], %[block], 0x08)
        MMI_SDC1(%[ftmp1], %[addr0], 0x00)
        MMI_SDC1(%[ftmp5], %[addr0], 0x08)
        PTR_ADDU   "%[block],   %[addr0],       %[line_size]            \n\t"

        MMI_ULDC1(%[ftmp0], %[pixels], 0x00)
        MMI_ULDC1(%[ftmp4], %[pixels], 0x08)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        MMI_ULDC1(%[ftmp1], %[pixels], 0x00)
        MMI_ULDC1(%[ftmp5], %[pixels], 0x08)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        MMI_ULDC1(%[ftmp2], %[block], 0x00)
        MMI_ULDC1(%[ftmp6], %[block], 0x08)
        PTR_ADDU   "%[addr0],   %[block],       %[line_size]            \n\t"
        MMI_ULDC1(%[ftmp3], %[addr0], 0x00)
        MMI_ULDC1(%[ftmp7], %[addr0], 0x08)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "pavgb      %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        MMI_SDC1(%[ftmp0], %[block], 0x00)
        MMI_SDC1(%[ftmp4], %[block], 0x08)
        MMI_SDC1(%[ftmp1], %[addr0], 0x00)
        MMI_SDC1(%[ftmp5], %[addr0], 0x08)
        PTR_ADDU   "%[block],   %[addr0],       %[line_size]            \n\t"

        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          RESTRICT_ASM_ALL64
          [addr0]"=&r"(addr[0]),
          [block]"+&r"(block),              [pixels]"+&r"(pixels),
          [h]"+&r"(h)
        : [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}

inline void ff_put_pixels4_l2_8_mmi(uint8_t *dst, const uint8_t *src1,
    const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
    int h)
{
    double ftmp[4];
    mips_reg addr[5];
    DECLARE_VAR_LOW32;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        PTR_ADDU   "%[addr2],   %[src_stride1], %[src_stride1]          \n\t"
        PTR_ADDU   "%[addr3],   %[src_stride2], %[src_stride2]          \n\t"
        PTR_ADDU   "%[addr4],   %[dst_stride],  %[dst_stride]           \n\t"
        "1:                                                             \n\t"
        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULWC1(%[ftmp0], %[src1], 0x00)
        MMI_ULWC1(%[ftmp1], %[addr0], 0x00)
        MMI_ULWC1(%[ftmp2], %[src2], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULWC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        MMI_SWC1(%[ftmp0], %[dst], 0x00)
        MMI_SWXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULWC1(%[ftmp0], %[src1], 0x00)
        MMI_ULWC1(%[ftmp1], %[addr0], 0x00)
        MMI_ULWC1(%[ftmp2], %[src2], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULWC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        MMI_SWC1(%[ftmp0], %[dst], 0x00)
        MMI_SWXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        PTR_ADDI   "%[h],       %[h],           -0x04                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          RESTRICT_ASM_LOW32
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),
          [dst]"+&r"(dst),                  [src1]"+&r"(src1),
          [src2]"+&r"(src2),                [h]"+&r"(h)
        : [dst_stride]"r"((mips_reg)dst_stride),
          [src_stride1]"r"((mips_reg)src_stride1),
          [src_stride2]"r"((mips_reg)src_stride2)
        : "memory"
    );
}

inline void ff_put_pixels8_l2_8_mmi(uint8_t *dst, const uint8_t *src1,
    const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
    int h)
{
    double ftmp[4];
    mips_reg addr[5];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        PTR_ADDU   "%[addr2],   %[src_stride1], %[src_stride1]          \n\t"
        PTR_ADDU   "%[addr3],   %[src_stride2], %[src_stride2]          \n\t"
        PTR_ADDU   "%[addr4],   %[dst_stride],  %[dst_stride]           \n\t"

        "1:                                                             \n\t"
        MMI_ULDC1(%[ftmp0], %[src1], 0x00)
        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULDC1(%[ftmp1], %[addr0], 0x00)
        MMI_ULDC1(%[ftmp2], %[src2], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULDC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        MMI_SDC1(%[ftmp0], %[dst], 0x00)
        MMI_SDXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        MMI_ULDC1(%[ftmp0], %[src1], 0x00)
        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULDC1(%[ftmp1], %[addr0], 0x00)
        MMI_ULDC1(%[ftmp2], %[src2], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULDC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        MMI_SDC1(%[ftmp0], %[dst], 0x00)
        MMI_SDXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        PTR_ADDI   "%[h],       %[h],           -0x04                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),
          [dst]"+&r"(dst),                  [src1]"+&r"(src1),
          [src2]"+&r"(src2),                [h]"+&r"(h)
        : [dst_stride]"r"((mips_reg)dst_stride),
          [src_stride1]"r"((mips_reg)src_stride1),
          [src_stride2]"r"((mips_reg)src_stride2)
        : "memory"
    );
}

inline void ff_put_pixels16_l2_8_mmi(uint8_t *dst, const uint8_t *src1,
    const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
    int h)
{
    double ftmp[8];
    mips_reg addr[5];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        PTR_ADDU   "%[addr2],   %[src_stride1], %[src_stride1]          \n\t"
        PTR_ADDU   "%[addr3],   %[src_stride2], %[src_stride2]          \n\t"
        PTR_ADDU   "%[addr4],   %[dst_stride],  %[dst_stride]           \n\t"

        "1:                                                             \n\t"
        MMI_ULDC1(%[ftmp0], %[src1], 0x00)
        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULDC1(%[ftmp4], %[src1], 0x08)
        MMI_ULDC1(%[ftmp1], %[addr0], 0x00)
        MMI_ULDC1(%[ftmp5], %[addr0], 0x08)
        MMI_ULDC1(%[ftmp2], %[src2], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULDC1(%[ftmp6], %[src2], 0x08)
        MMI_ULDC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        MMI_ULDC1(%[ftmp7], %[addr1], 0x08)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "pavgb      %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        MMI_SDC1(%[ftmp0], %[dst], 0x00)
        MMI_SDXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        MMI_SDC1(%[ftmp4], %[dst], 0x08)
        MMI_SDXC1(%[ftmp5], %[dst], %[dst_stride], 0x08)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        MMI_ULDC1(%[ftmp0], %[src1], 0x00)
        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULDC1(%[ftmp4], %[src1], 0x08)
        MMI_ULDC1(%[ftmp1], %[addr0], 0x00)
        MMI_ULDC1(%[ftmp5], %[addr0], 0x08)
        MMI_ULDC1(%[ftmp2], %[src2], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULDC1(%[ftmp6], %[src2], 0x08)
        MMI_ULDC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        MMI_ULDC1(%[ftmp7], %[addr1], 0x08)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "pavgb      %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        MMI_SDC1(%[ftmp0], %[dst], 0x00)
        MMI_SDXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        MMI_SDC1(%[ftmp4], %[dst], 0x08)
        MMI_SDXC1(%[ftmp5], %[dst], %[dst_stride], 0x08)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        PTR_ADDI   "%[h],       %[h],           -0x04                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),
          [dst]"+&r"(dst),                  [src1]"+&r"(src1),
          [src2]"+&r"(src2),                [h]"+&r"(h)
        : [dst_stride]"r"((mips_reg)dst_stride),
          [src_stride1]"r"((mips_reg)src_stride1),
          [src_stride2]"r"((mips_reg)src_stride2)
        : "memory"
    );
}

inline void ff_avg_pixels4_l2_8_mmi(uint8_t *dst, const uint8_t *src1,
    const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
    int h)
{
    double ftmp[6];
    mips_reg addr[6];
    DECLARE_VAR_LOW32;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        PTR_ADDU   "%[addr2],   %[src_stride1], %[src_stride1]          \n\t"
        PTR_ADDU   "%[addr3],   %[src_stride2], %[src_stride2]          \n\t"
        PTR_ADDU   "%[addr4],   %[dst_stride],  %[dst_stride]           \n\t"

        "1:                                                             \n\t"
        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULWC1(%[ftmp0], %[src1], 0x00)
        MMI_ULWC1(%[ftmp1], %[addr0], 0x00)
        MMI_ULWC1(%[ftmp2], %[src2], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULWC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        PTR_ADDU   "%[addr5],   %[dst],         %[dst_stride]           \n\t"
        MMI_ULWC1(%[ftmp4], %[dst], 0x00)
        MMI_ULWC1(%[ftmp5], %[addr5], 0x00)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        MMI_SWC1(%[ftmp0], %[dst], 0x00)
        MMI_SWXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULWC1(%[ftmp0], %[src1], 0x00)
        MMI_ULWC1(%[ftmp1], %[addr0], 0x00)
        MMI_ULWC1(%[ftmp2], %[src2], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULWC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        PTR_ADDU   "%[addr5],   %[dst],         %[dst_stride]           \n\t"
        MMI_ULWC1(%[ftmp4], %[dst], 0x00)
        MMI_ULWC1(%[ftmp5], %[addr5], 0x00)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        MMI_SWC1(%[ftmp0], %[dst], 0x00)
        MMI_SWXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        PTR_ADDI   "%[h],       %[h],           -0x04                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          RESTRICT_ASM_LOW32
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),            [addr5]"=&r"(addr[5]),
          [dst]"+&r"(dst),                  [src1]"+&r"(src1),
          [src2]"+&r"(src2),                [h]"+&r"(h)
        : [dst_stride]"r"((mips_reg)dst_stride),
          [src_stride1]"r"((mips_reg)src_stride1),
          [src_stride2]"r"((mips_reg)src_stride2)
        : "memory"
    );
}

inline void ff_avg_pixels8_l2_8_mmi(uint8_t *dst, const uint8_t *src1,
    const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
    int h)
{
    double ftmp[6];
    mips_reg addr[6];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        PTR_ADDU   "%[addr2],   %[src_stride1], %[src_stride1]          \n\t"
        PTR_ADDU   "%[addr3],   %[src_stride2], %[src_stride2]          \n\t"
        PTR_ADDU   "%[addr4],   %[dst_stride],  %[dst_stride]           \n\t"

        "1:                                                             \n\t"
        MMI_ULDC1(%[ftmp0], %[src1], 0x00)
        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULDC1(%[ftmp1], %[addr0], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULDC1(%[ftmp2], %[src2], 0x00)
        MMI_ULDC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        PTR_ADDU   "%[addr5],   %[dst],         %[dst_stride]           \n\t"
        MMI_ULDC1(%[ftmp4], %[dst], 0x00)
        MMI_ULDC1(%[ftmp5], %[addr5], 0x00)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        MMI_SDC1(%[ftmp0], %[dst], 0x00)
        MMI_SDXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        MMI_ULDC1(%[ftmp0], %[src1], 0x00)
        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULDC1(%[ftmp1], %[addr0], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULDC1(%[ftmp2], %[src2], 0x00)
        MMI_ULDC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        PTR_ADDU   "%[addr5],   %[dst],         %[dst_stride]           \n\t"
        MMI_ULDC1(%[ftmp4], %[dst], 0x00)
        MMI_ULDC1(%[ftmp5], %[addr5], 0x00)
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        MMI_SDC1(%[ftmp0], %[dst], 0x00)
        MMI_SDXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        PTR_ADDI   "%[h],       %[h],           -0x04                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),            [addr5]"=&r"(addr[5]),
          [dst]"+&r"(dst),                  [src1]"+&r"(src1),
          [src2]"+&r"(src2),                [h]"+&r"(h)
        : [dst_stride]"r"((mips_reg)dst_stride),
          [src_stride1]"r"((mips_reg)src_stride1),
          [src_stride2]"r"((mips_reg)src_stride2)
        : "memory"
    );
}

inline void ff_avg_pixels16_l2_8_mmi(uint8_t *dst, const uint8_t *src1,
    const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
    int h)
{
    ff_avg_pixels8_l2_8_mmi(dst, src1, src2, dst_stride, src_stride1,
            src_stride2, h);
    ff_avg_pixels8_l2_8_mmi(dst + 8, src1 + 8, src2 + 8, dst_stride,
            src_stride1, src_stride2, h);
}

void ff_put_pixels4_x2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_pixels4_l2_8_mmi(block, pixels, pixels + 1, line_size, line_size,
            line_size, h);
}

void ff_put_pixels8_x2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_pixels8_l2_8_mmi(block, pixels, pixels + 1, line_size, line_size,
            line_size, h);
}

void ff_put_pixels16_x2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_pixels16_l2_8_mmi(block, pixels, pixels + 1, line_size, line_size,
            line_size, h);
}

void ff_avg_pixels4_x2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_avg_pixels4_l2_8_mmi(block, pixels, pixels + 1, line_size, line_size,
            line_size, h);
}

void ff_avg_pixels8_x2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_avg_pixels8_l2_8_mmi(block, pixels, pixels + 1, line_size, line_size,
            line_size, h);
}

void ff_avg_pixels16_x2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_avg_pixels8_x2_8_mmi(block, pixels, line_size, h);
    ff_avg_pixels8_x2_8_mmi(block + 8, pixels + 8, line_size, h);
}

inline void ff_put_no_rnd_pixels8_l2_8_mmi(uint8_t *dst, const uint8_t *src1,
    const uint8_t *src2, int dst_stride, int src_stride1, int src_stride2,
    int h)
{
    double ftmp[5];
    mips_reg addr[5];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        "pcmpeqb    %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        PTR_ADDU   "%[addr2],   %[src_stride1], %[src_stride1]          \n\t"
        PTR_ADDU   "%[addr3],   %[src_stride2], %[src_stride2]          \n\t"
        PTR_ADDU   "%[addr4],   %[dst_stride],  %[dst_stride]           \n\t"

        "1:                                                             \n\t"
        MMI_ULDC1(%[ftmp0], %[src1], 0x00)
        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULDC1(%[ftmp1], %[addr0], 0x00)
        MMI_ULDC1(%[ftmp2], %[src2], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULDC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp4]                \n\t"
        "xor        %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "xor        %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp4]                \n\t"
        MMI_SDC1(%[ftmp0], %[dst], 0x00)
        MMI_SDXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        MMI_ULDC1(%[ftmp0], %[src1], 0x00)
        PTR_ADDU   "%[addr0],   %[src1],        %[src_stride1]          \n\t"
        MMI_ULDC1(%[ftmp1], %[addr0], 0x00)
        MMI_ULDC1(%[ftmp2], %[src2], 0x00)
        PTR_ADDU   "%[addr1],   %[src2],        %[src_stride2]          \n\t"
        MMI_ULDC1(%[ftmp3], %[addr1], 0x00)
        PTR_ADDU   "%[src1],    %[src1],        %[addr2]                \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp4]                \n\t"
        "xor        %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "xor        %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "pavgb      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "xor        %[ftmp1],   %[ftmp1],       %[ftmp4]                \n\t"
        MMI_SDC1(%[ftmp0], %[dst], 0x00)
        MMI_SDXC1(%[ftmp1], %[dst], %[dst_stride], 0x00)
        PTR_ADDU   "%[src2],    %[src2],        %[addr3]                \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[addr4]                \n\t"

        PTR_ADDI   "%[h],       %[h],           -0x04                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),
          [dst]"+&r"(dst),                  [src1]"+&r"(src1),
          [src2]"+&r"(src2),                [h]"+&r"(h)
        : [dst_stride]"r"((mips_reg)dst_stride),
          [src_stride1]"r"((mips_reg)src_stride1),
          [src_stride2]"r"((mips_reg)src_stride2)
        : "memory"
    );
}

void ff_put_no_rnd_pixels8_x2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_no_rnd_pixels8_l2_8_mmi(block, pixels, pixels + 1, line_size,
            line_size, line_size, h);
}

void ff_put_no_rnd_pixels16_x2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_no_rnd_pixels8_x2_8_mmi(block, pixels, line_size, h);
    ff_put_no_rnd_pixels8_x2_8_mmi(block + 8, pixels + 8, line_size, h);
}

void ff_put_pixels4_y2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_pixels4_l2_8_mmi(block, pixels, pixels + line_size, line_size,
            line_size, line_size, h);
}

void ff_put_pixels8_y2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_pixels8_l2_8_mmi(block, pixels, pixels + line_size, line_size,
            line_size, line_size, h);
}

void ff_put_pixels16_y2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_pixels16_l2_8_mmi(block, pixels, pixels + line_size, line_size,
            line_size, line_size, h);
}

void ff_avg_pixels4_y2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_avg_pixels4_l2_8_mmi(block, pixels, pixels + line_size, line_size,
            line_size, line_size, h);
}

void ff_avg_pixels8_y2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_avg_pixels8_l2_8_mmi(block, pixels, pixels + line_size, line_size,
            line_size, line_size, h);
}

void ff_avg_pixels16_y2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_avg_pixels8_y2_8_mmi(block, pixels, line_size, h);
    ff_avg_pixels8_y2_8_mmi(block + 8, pixels + 8, line_size, h);
}

void ff_put_no_rnd_pixels8_y2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_no_rnd_pixels8_l2_8_mmi(block, pixels, pixels + line_size,
            line_size, line_size, line_size, h);
}

void ff_put_no_rnd_pixels16_y2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_no_rnd_pixels8_y2_8_mmi(block, pixels, line_size, h);
    ff_put_no_rnd_pixels8_y2_8_mmi(block + 8 , pixels + 8, line_size, h);
}

void ff_put_pixels4_xy2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    /* FIXME HIGH BIT DEPTH */
    int i;
    const uint32_t a = AV_RN32(pixels);
    const uint32_t b = AV_RN32(pixels + 1);
    uint32_t l0 = (a & 0x03030303UL) +
                  (b & 0x03030303UL) +
                       0x02020202UL;
    uint32_t h0 = ((a & 0xFCFCFCFCUL) >> 2) +
                  ((b & 0xFCFCFCFCUL) >> 2);
    uint32_t l1, h1;

    pixels += line_size;
    for (i = 0; i < h; i += 2) {
        uint32_t a = AV_RN32(pixels);
        uint32_t b = AV_RN32(pixels + 1);
        l1 = (a & 0x03030303UL) +
             (b & 0x03030303UL);
        h1 = ((a & 0xFCFCFCFCUL) >> 2) +
             ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) = h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
        pixels += line_size;
        block  += line_size;
        a  = AV_RN32(pixels);
        b  = AV_RN32(pixels + 1);
        l0 = (a & 0x03030303UL) +
             (b & 0x03030303UL) +
                  0x02020202UL;
        h0 = ((a & 0xFCFCFCFCUL) >> 2) +
             ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) = h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
        pixels += line_size;
        block  += line_size;
    }
}

void ff_put_pixels8_xy2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
#if 1
    double ftmp[10];
    mips_reg addr[2];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "dli        %[addr0],   0x0f                                    \n\t"
        "pcmpeqw    %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "dmtc1      %[addr0],   %[ftmp8]                                \n\t"
        "dli        %[addr0],   0x01                                    \n\t"
        "psrlh      %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "dmtc1      %[addr0],   %[ftmp8]                                \n\t"
        "psllh      %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"

        "dli        %[addr0],   0x02                                    \n\t"
        "dmtc1      %[addr0],   %[ftmp9]                                \n\t"
        MMI_ULDC1(%[ftmp0], %[pixels], 0x00)
        MMI_ULDC1(%[ftmp4], %[pixels], 0x01)
        "mov.d      %[ftmp1],   %[ftmp0]                                \n\t"
        "mov.d      %[ftmp5],   %[ftmp4]                                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]                \n\t"
        "punpckhbh  %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "punpckhbh  %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "paddush    %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "paddush    %[ftmp5],   %[ftmp5],       %[ftmp1]                \n\t"
        "xor        %[addr0],   %[addr0],       %[addr0]                \n\t"
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        ".p2align   3                                                   \n\t"

        "1:                                                             \n\t"
        PTR_ADDU   "%[addr1],   %[pixels],      %[addr0]                \n\t"
        MMI_ULDC1(%[ftmp0], %[addr1], 0x00)
        MMI_ULDC1(%[ftmp2], %[addr1], 0x01)
        "mov.d      %[ftmp1],   %[ftmp0]                                \n\t"
        "mov.d      %[ftmp3],   %[ftmp2]                                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "punpckhbh  %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "paddush    %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "paddush    %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "paddush    %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "paddush    %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "paddush    %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "paddush    %[ftmp5],   %[ftmp5],       %[ftmp1]                \n\t"
        "psrlh      %[ftmp4],   %[ftmp4],       %[ftmp9]                \n\t"
        "psrlh      %[ftmp5],   %[ftmp5],       %[ftmp9]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        MMI_SDXC1(%[ftmp4], %[block], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[line_size]            \n\t"
        PTR_ADDU   "%[addr1],   %[pixels],      %[addr0]                \n\t"
        MMI_ULDC1(%[ftmp2], %[addr1], 0x00)
        MMI_ULDC1(%[ftmp4], %[addr1], 0x01)
        "mov.d      %[ftmp3],   %[ftmp2]                                \n\t"
        "mov.d      %[ftmp5],   %[ftmp4]                                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp7]                \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "punpckhbh  %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "paddush    %[ftmp4],   %[ftmp4],       %[ftmp2]                \n\t"
        "paddush    %[ftmp5],   %[ftmp5],       %[ftmp3]                \n\t"
        "paddush    %[ftmp0],   %[ftmp0],       %[ftmp6]                \n\t"
        "paddush    %[ftmp1],   %[ftmp1],       %[ftmp6]                \n\t"
        "paddush    %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "paddush    %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        "psrlh      %[ftmp0],   %[ftmp0],       %[ftmp9]                \n\t"
        "psrlh      %[ftmp1],   %[ftmp1],       %[ftmp9]                \n\t"
        "packushb   %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        MMI_SDXC1(%[ftmp0], %[block], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[line_size]            \n\t"
        PTR_ADDU   "%[h],       %[h],           -0x02                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [h]"+&r"(h),                      [pixels]"+&r"(pixels)
        : [block]"r"(block),                [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
#else
    /* FIXME HIGH BIT DEPTH */
    int j;

    for (j = 0; j < 2; j++) {
        int i;
        const uint32_t a = AV_RN32(pixels);
        const uint32_t b = AV_RN32(pixels + 1);
        uint32_t l0 = (a & 0x03030303UL) +
                      (b & 0x03030303UL) +
                           0x02020202UL;
        uint32_t h0 = ((a & 0xFCFCFCFCUL) >> 2) +
                      ((b & 0xFCFCFCFCUL) >> 2);
        uint32_t l1, h1;

        pixels += line_size;
        for (i = 0; i < h; i += 2) {
            uint32_t a = AV_RN32(pixels);
            uint32_t b = AV_RN32(pixels + 1);
            l1 = (a & 0x03030303UL) +
                 (b & 0x03030303UL);
            h1 = ((a & 0xFCFCFCFCUL) >> 2) +
                 ((b & 0xFCFCFCFCUL) >> 2);
            *((uint32_t *) block) = h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
            pixels += line_size;
            block  += line_size;
            a  = AV_RN32(pixels);
            b  = AV_RN32(pixels + 1);
            l0 = (a & 0x03030303UL) +
                 (b & 0x03030303UL) +
                      0x02020202UL;
            h0 = ((a & 0xFCFCFCFCUL) >> 2) +
                 ((b & 0xFCFCFCFCUL) >> 2);
            *((uint32_t *) block) = h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
            pixels += line_size;
            block  += line_size;
        }
        pixels += 4 - line_size * (h + 1);
        block  += 4 - line_size * h;
    }
#endif
}

void ff_put_pixels16_xy2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_pixels8_xy2_8_mmi(block, pixels, line_size, h);
    ff_put_pixels8_xy2_8_mmi(block + 8, pixels + 8, line_size, h);
}

void ff_avg_pixels4_xy2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    /* FIXME HIGH BIT DEPTH */
    int i;
    const uint32_t a = AV_RN32(pixels);
    const uint32_t b = AV_RN32(pixels + 1);
    uint32_t l0 = (a & 0x03030303UL) +
                  (b & 0x03030303UL) +
                       0x02020202UL;
    uint32_t h0 = ((a & 0xFCFCFCFCUL) >> 2) +
                  ((b & 0xFCFCFCFCUL) >> 2);
    uint32_t l1, h1;

    pixels += line_size;
    for (i = 0; i < h; i += 2) {
        uint32_t a = AV_RN32(pixels);
        uint32_t b = AV_RN32(pixels + 1);
        l1 = (a & 0x03030303UL) +
             (b & 0x03030303UL);
        h1 = ((a & 0xFCFCFCFCUL) >> 2) +
             ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) = rnd_avg32(*((uint32_t *) block), h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL));
        pixels += line_size;
        block  += line_size;
        a  = AV_RN32(pixels);
        b  = AV_RN32(pixels + 1);
        l0 = (a & 0x03030303UL) +
             (b & 0x03030303UL) +
                  0x02020202UL;
        h0 = ((a & 0xFCFCFCFCUL) >> 2) +
             ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) = rnd_avg32(*((uint32_t *) block), h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL));
        pixels += line_size;
        block  += line_size;
    }
}

void ff_avg_pixels8_xy2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    /* FIXME HIGH BIT DEPTH */
    int j;

    for (j = 0; j < 2; j++) {
        int i;
        const uint32_t a = AV_RN32(pixels);
        const uint32_t b = AV_RN32(pixels + 1);
        uint32_t l0 = (a & 0x03030303UL) +
                      (b & 0x03030303UL) +
                           0x02020202UL;
        uint32_t h0 = ((a & 0xFCFCFCFCUL) >> 2) +
                      ((b & 0xFCFCFCFCUL) >> 2);
        uint32_t l1, h1;

        pixels += line_size;
        for (i = 0; i < h; i += 2) {
            uint32_t a = AV_RN32(pixels);
            uint32_t b = AV_RN32(pixels + 1);
            l1 = (a & 0x03030303UL) +
                 (b & 0x03030303UL);
            h1 = ((a & 0xFCFCFCFCUL) >> 2) +
                 ((b & 0xFCFCFCFCUL) >> 2);
            *((uint32_t *) block) = rnd_avg32(*((uint32_t *) block), h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL));
            pixels += line_size;
            block  += line_size;
            a  = AV_RN32(pixels);
            b  = AV_RN32(pixels + 1);
            l0 = (a & 0x03030303UL) +
                 (b & 0x03030303UL) +
                      0x02020202UL;
            h0 = ((a & 0xFCFCFCFCUL) >> 2) +
                 ((b & 0xFCFCFCFCUL) >> 2);
            *((uint32_t *) block) = rnd_avg32(*((uint32_t *) block), h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL));
            pixels += line_size;
            block  += line_size;
        }
        pixels += 4 - line_size * (h + 1);
        block  += 4 - line_size * h;
    }
}

void ff_avg_pixels16_xy2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_avg_pixels8_xy2_8_mmi(block, pixels, line_size, h);
    ff_avg_pixels8_xy2_8_mmi(block + 8, pixels + 8, line_size, h);
}

void ff_put_no_rnd_pixels8_xy2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    /* FIXME HIGH BIT DEPTH */
    int j;

    for (j = 0; j < 2; j++) {
        int i;
        const uint32_t a = AV_RN32(pixels);
        const uint32_t b = AV_RN32(pixels + 1);
        uint32_t l0 = (a & 0x03030303UL) +
                      (b & 0x03030303UL) +
                           0x01010101UL;
        uint32_t h0 = ((a & 0xFCFCFCFCUL) >> 2) +
                      ((b & 0xFCFCFCFCUL) >> 2);
        uint32_t l1, h1;

        pixels += line_size;
        for (i = 0; i < h; i += 2) {
            uint32_t a = AV_RN32(pixels);
            uint32_t b = AV_RN32(pixels + 1);
            l1 = (a & 0x03030303UL) +
                 (b & 0x03030303UL);
            h1 = ((a & 0xFCFCFCFCUL) >> 2) +
                 ((b & 0xFCFCFCFCUL) >> 2);
            *((uint32_t *) block) = h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
            pixels += line_size;
            block  += line_size;
            a  = AV_RN32(pixels);
            b  = AV_RN32(pixels + 1);
            l0 = (a & 0x03030303UL) +
                 (b & 0x03030303UL) +
                      0x01010101UL;
            h0 = ((a & 0xFCFCFCFCUL) >> 2) +
                 ((b & 0xFCFCFCFCUL) >> 2);
            *((uint32_t *) block) = h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
            pixels += line_size;
            block  += line_size;
        }
        pixels += 4 - line_size * (h + 1);
        block  += 4 - line_size * h;
    }
}

void ff_put_no_rnd_pixels16_xy2_8_mmi(uint8_t *block, const uint8_t *pixels,
    ptrdiff_t line_size, int h)
{
    ff_put_no_rnd_pixels8_xy2_8_mmi(block, pixels, line_size, h);
    ff_put_no_rnd_pixels8_xy2_8_mmi(block + 8, pixels + 8, line_size, h);
}
