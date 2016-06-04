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
#include "libavutil/mips/asmdefs.h"

void ff_get_pixels_8_mmi(int16_t *av_restrict block, const uint8_t *pixels,
        ptrdiff_t line_size)
{
    double ftmp[6];
    mips_reg tmp[2];

    __asm__ volatile (
        "li         %[tmp1],    0x08                                    \n\t"
        "move       %[tmp0],    $0                                      \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "1:                                                             \n\t"
        "gsldlc1    %[ftmp1],   0x07(%[pixels])                         \n\t"
        "gsldrc1    %[ftmp1],   0x00(%[pixels])                         \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp5],   %[ftmp1],       %[ftmp0]                \n\t"
        "gssdxc1    %[ftmp2],   0x00(%[block],  %[tmp0])                \n\t"
        "gssdxc1    %[ftmp5],   0x08(%[block],  %[tmp0])                \n\t"
        PTR_ADDI   "%[tmp1],    %[tmp1],       -0x01                    \n\t"
        PTR_ADDIU  "%[tmp0],    %[tmp0],        0x10                    \n\t"
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        "bnez       %[tmp1],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          [pixels]"+&r"(pixels)
        : [block]"r"((mips_reg)block),      [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}

void ff_diff_pixels_mmi(int16_t *av_restrict block, const uint8_t *src1,
        const uint8_t *src2, int stride)
{
    double ftmp[5];
    mips_reg tmp[1];

    __asm__ volatile (
        "li         %[tmp0],    0x08                                    \n\t"
        "xor        %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        "1:                                                             \n\t"
        "gsldlc1    %[ftmp0],   0x07(%[src1])                           \n\t"
        "gsldrc1    %[ftmp0],   0x00(%[src1])                           \n\t"
        "or         %[ftmp1],   %[ftmp0],       %[ftmp0]                \n\t"
        "gsldlc1    %[ftmp2],   0x07(%[src2])                           \n\t"
        "gsldrc1    %[ftmp2],   0x00(%[src2])                           \n\t"
        "or         %[ftmp3],   %[ftmp2],       %[ftmp2]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpckhbh  %[ftmp1],   %[ftmp1],       %[ftmp4]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubh      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "gssdlc1    %[ftmp0],   0x07(%[block])                          \n\t"
        "gssdrc1    %[ftmp0],   0x00(%[block])                          \n\t"
        "gssdlc1    %[ftmp1],   0x0f(%[block])                          \n\t"
        "gssdrc1    %[ftmp1],   0x08(%[block])                          \n\t"
        PTR_ADDI   "%[tmp0],    %[tmp0], -0x01                          \n\t"
        PTR_ADDIU  "%[block],   %[block], 0x10                          \n\t"
        PTR_ADDU   "%[src1],    %[src1],        %[stride]               \n\t"
        PTR_ADDU   "%[src2],    %[src2],        %[stride]               \n\t"
        "bgtz       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),
          [tmp0]"=&r"(tmp[0]),
          [block]"+&r"(block),              [src1]"+&r"(src1),
          [src2]"+&r"(src2)
        : [stride]"r"((mips_reg)stride)
        : "memory"
    );
}
