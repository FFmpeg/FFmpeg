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
#include "libavutil/mips/mmiutils.h"

void ff_get_pixels_8_mmi(int16_t *restrict block, const uint8_t *pixels,
                         ptrdiff_t stride)
{
    double ftmp[7];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"

        MMI_LDC1(%[ftmp1], %[pixels], 0x00)
        MMI_LDXC1(%[ftmp2], %[pixels], %[stride], 0x00)
        "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]                \n\t"
        MMI_SDC1(%[ftmp3], %[block], 0x00)
        MMI_SDC1(%[ftmp4], %[block], 0x08)
        MMI_SDC1(%[ftmp5], %[block], 0x10)
        MMI_SDC1(%[ftmp6], %[block], 0x18)
        PTR_ADDU   "%[pixels],  %[pixels],      %[stride_x2]            \n\t"

        MMI_LDC1(%[ftmp1], %[pixels], 0x00)
        MMI_LDXC1(%[ftmp2], %[pixels], %[stride], 0x00)
        "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]                \n\t"
        MMI_SDC1(%[ftmp3], %[block], 0x20)
        MMI_SDC1(%[ftmp4], %[block], 0x28)
        MMI_SDC1(%[ftmp5], %[block], 0x30)
        MMI_SDC1(%[ftmp6], %[block], 0x38)
        PTR_ADDU   "%[pixels],  %[pixels],      %[stride_x2]            \n\t"

        MMI_LDC1(%[ftmp1], %[pixels], 0x00)
        MMI_LDXC1(%[ftmp2], %[pixels], %[stride], 0x00)
        "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]                \n\t"
        MMI_SDC1(%[ftmp3], %[block], 0x40)
        MMI_SDC1(%[ftmp4], %[block], 0x48)
        MMI_SDC1(%[ftmp5], %[block], 0x50)
        MMI_SDC1(%[ftmp6], %[block], 0x58)
        PTR_ADDU   "%[pixels],  %[pixels],      %[stride_x2]            \n\t"

        MMI_LDC1(%[ftmp1], %[pixels], 0x00)
        MMI_LDXC1(%[ftmp2], %[pixels], %[stride], 0x00)
        "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]                \n\t"
        MMI_SDC1(%[ftmp3], %[block], 0x60)
        MMI_SDC1(%[ftmp4], %[block], 0x68)
        MMI_SDC1(%[ftmp5], %[block], 0x70)
        MMI_SDC1(%[ftmp6], %[block], 0x78)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [pixels]"+&r"(pixels)
        : [block]"r"((mips_reg)block),      [stride]"r"((mips_reg)stride),
          [stride_x2]"r"((mips_reg)(stride<<1))
        : "memory"
    );
}

void ff_diff_pixels_mmi(int16_t *restrict block, const uint8_t *src1,
        const uint8_t *src2, ptrdiff_t stride)
{
    double ftmp[5];
    mips_reg tmp[1];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "li         %[tmp0],    0x08                                    \n\t"
        "pxor       %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        "1:                                                             \n\t"
        MMI_LDC1(%[ftmp0], %[src1], 0x00)
        "por        %[ftmp1],   %[ftmp0],       %[ftmp0]                \n\t"
        MMI_LDC1(%[ftmp2], %[src2], 0x00)
        "por        %[ftmp3],   %[ftmp2],       %[ftmp2]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpckhbh  %[ftmp1],   %[ftmp1],       %[ftmp4]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubh      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        MMI_SDC1(%[ftmp0], %[block], 0x00)
        MMI_SDC1(%[ftmp1], %[block], 0x08)
        PTR_ADDI   "%[tmp0],    %[tmp0], -0x01                          \n\t"
        PTR_ADDIU  "%[block],   %[block], 0x10                          \n\t"
        PTR_ADDU   "%[src1],    %[src1],        %[stride]               \n\t"
        PTR_ADDU   "%[src2],    %[src2],        %[stride]               \n\t"
        "bgtz       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [block]"+&r"(block),              [src1]"+&r"(src1),
          [src2]"+&r"(src2)
        : [stride]"r"((mips_reg)stride)
        : "memory"
    );
}
