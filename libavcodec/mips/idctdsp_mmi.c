/*
 * Loongson SIMD optimized idctdsp
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

#include "idctdsp_mips.h"
#include "constants.h"
#include "libavutil/mips/mmiutils.h"

void ff_put_pixels_clamped_mmi(const int16_t *block,
        uint8_t *av_restrict pixels, ptrdiff_t line_size)
{
    double ftmp[8];

    __asm__ volatile (
        MMI_LDC1(%[ftmp0], %[block], 0x00)
        MMI_LDC1(%[ftmp1], %[block], 0x08)
        MMI_LDC1(%[ftmp2], %[block], 0x10)
        MMI_LDC1(%[ftmp3], %[block], 0x18)
        MMI_LDC1(%[ftmp4], %[block], 0x20)
        MMI_LDC1(%[ftmp5], %[block], 0x28)
        MMI_LDC1(%[ftmp6], %[block], 0x30)
        MMI_LDC1(%[ftmp7], %[block], 0x38)
        "packushb   %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "packushb   %[ftmp6],   %[ftmp6],       %[ftmp7]                \n\t"
        MMI_SDC1(%[ftmp0], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        MMI_SDC1(%[ftmp2], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        MMI_SDC1(%[ftmp4], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        MMI_SDC1(%[ftmp6], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"

        MMI_LDC1(%[ftmp0], %[block], 0x40)
        MMI_LDC1(%[ftmp1], %[block], 0x48)
        MMI_LDC1(%[ftmp2], %[block], 0x50)
        MMI_LDC1(%[ftmp3], %[block], 0x58)
        MMI_LDC1(%[ftmp4], %[block], 0x60)
        MMI_LDC1(%[ftmp5], %[block], 0x68)
        MMI_LDC1(%[ftmp6], %[block], 0x70)
        MMI_LDC1(%[ftmp7], %[block], 0x78)
        "packushb   %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "packushb   %[ftmp6],   %[ftmp6],       %[ftmp7]                \n\t"
        MMI_SDC1(%[ftmp0], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        MMI_SDC1(%[ftmp2], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        MMI_SDC1(%[ftmp4], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],      %[line_size]            \n\t"
        MMI_SDC1(%[ftmp6], %[pixels], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [pixels]"+&r"(pixels)
        : [line_size]"r"((mips_reg)line_size),
          [block]"r"(block)
        : "memory"
    );
}

void ff_put_signed_pixels_clamped_mmi(const int16_t *block,
    uint8_t *av_restrict pixels, ptrdiff_t line_size)
{
    double ftmp[5];

    __asm__ volatile (
        MMI_LDC1(%[ftmp1], %[block], 0x00)
        MMI_LDC1(%[ftmp0], %[block], 0x08)
        "packsshb   %[ftmp1],       %[ftmp1],       %[ftmp0]            \n\t"
        MMI_LDC1(%[ftmp2], %[block], 0x10)
        MMI_LDC1(%[ftmp0], %[block], 0x18)
        "packsshb   %[ftmp2],       %[ftmp2],       %[ftmp0]            \n\t"
        MMI_LDC1(%[ftmp3], %[block], 0x20)
        MMI_LDC1(%[ftmp0], %[block], 0x28)
        "packsshb   %[ftmp3],       %[ftmp3],       %[ftmp0]            \n\t"
        MMI_LDC1(%[ftmp4], %[block], 0x30)
        MMI_LDC1(%[ftmp0], %[block], 0x38)
        "packsshb   %[ftmp4],       %[ftmp4],       %[ftmp0]            \n\t"
        "paddb      %[ftmp1],       %[ftmp1],       %[ff_pb_80]         \n\t"
        "paddb      %[ftmp2],       %[ftmp2],       %[ff_pb_80]         \n\t"
        "paddb      %[ftmp3],       %[ftmp3],       %[ff_pb_80]         \n\t"
        "paddb      %[ftmp4],       %[ftmp4],       %[ff_pb_80]         \n\t"
        MMI_SDC1(%[ftmp1], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],      %[pixels],      %[line_size]        \n\t"
        MMI_SDC1(%[ftmp2], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],      %[pixels],      %[line_size]        \n\t"
        MMI_SDC1(%[ftmp3], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],      %[pixels],      %[line_size]        \n\t"
        MMI_SDC1(%[ftmp4], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],      %[pixels],      %[line_size]        \n\t"

        MMI_LDC1(%[ftmp1], %[block], 0x40)
        MMI_LDC1(%[ftmp0], %[block], 0x48)
        "packsshb   %[ftmp1],       %[ftmp1],       %[ftmp0]            \n\t"
        MMI_LDC1(%[ftmp2], %[block], 0x50)
        MMI_LDC1(%[ftmp0], %[block], 0x58)
        "packsshb   %[ftmp2],       %[ftmp2],       %[ftmp0]            \n\t"
        MMI_LDC1(%[ftmp3], %[block], 0x60)
        MMI_LDC1(%[ftmp0], %[block], 0x68)
        "packsshb   %[ftmp3],       %[ftmp3],       %[ftmp0]            \n\t"
        MMI_LDC1(%[ftmp4], %[block], 0x70)
        MMI_LDC1(%[ftmp0], %[block], 0x78)
        "packsshb   %[ftmp4],       %[ftmp4],       %[ftmp0]            \n\t"
        "paddb      %[ftmp1],       %[ftmp1],       %[ff_pb_80]         \n\t"
        "paddb      %[ftmp2],       %[ftmp2],       %[ff_pb_80]         \n\t"
        "paddb      %[ftmp3],       %[ftmp3],       %[ff_pb_80]         \n\t"
        "paddb      %[ftmp4],       %[ftmp4],       %[ff_pb_80]         \n\t"
        MMI_SDC1(%[ftmp1], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],      %[pixels],      %[line_size]        \n\t"
        MMI_SDC1(%[ftmp2], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],      %[pixels],      %[line_size]        \n\t"
        MMI_SDC1(%[ftmp3], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],      %[pixels],      %[line_size]        \n\t"
        MMI_SDC1(%[ftmp4], %[pixels], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),
          [pixels]"+&r"(pixels)
        : [block]"r"(block),
          [line_size]"r"((mips_reg)line_size),
          [ff_pb_80]"f"(ff_pb_80.f)
        : "memory"
    );
}

void ff_add_pixels_clamped_mmi(const int16_t *block,
        uint8_t *av_restrict pixels, ptrdiff_t line_size)
{
    double ftmp[9];
    uint64_t tmp[1];
    __asm__ volatile (
        "li         %[tmp0],    0x04                           \n\t"
        "pxor       %[ftmp0],   %[ftmp0],   %[ftmp0]           \n\t"
        "1:                                                    \n\t"
        MMI_LDC1(%[ftmp5], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],  %[line_size]       \n\t"
        MMI_LDC1(%[ftmp6], %[pixels], 0x00)
        PTR_SUBU   "%[pixels],  %[pixels],  %[line_size]       \n\t"
        MMI_LDC1(%[ftmp1], %[block], 0x00)
        MMI_LDC1(%[ftmp2], %[block], 0x08)
        MMI_LDC1(%[ftmp3], %[block], 0x10)
        MMI_LDC1(%[ftmp4], %[block], 0x18)
        PTR_ADDIU  "%[block],   %[block],   0x20               \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp5],   %[ftmp0]           \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],   %[ftmp0]           \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp6],   %[ftmp0]           \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],   %[ftmp0]           \n\t"
        "paddh      %[ftmp1],   %[ftmp1],   %[ftmp5]           \n\t"
        "paddh      %[ftmp2],   %[ftmp2],   %[ftmp7]           \n\t"
        "paddh      %[ftmp3],   %[ftmp3],   %[ftmp6]           \n\t"
        "paddh      %[ftmp4],   %[ftmp4],   %[ftmp8]           \n\t"
        "packushb   %[ftmp1],   %[ftmp1],   %[ftmp2]           \n\t"
        "packushb   %[ftmp3],   %[ftmp3],   %[ftmp4]           \n\t"
        MMI_SDC1(%[ftmp1], %[pixels], 0x00)
        PTR_ADDU   "%[pixels],  %[pixels],  %[line_size]       \n\t"
        MMI_SDC1(%[ftmp3], %[pixels], 0x00)
        "addi       %[tmp0],    %[tmp0],    -0x01              \n\t"
        PTR_ADDU   "%[pixels],  %[pixels],  %[line_size]       \n\t"
        "bnez       %[tmp0],    1b                             \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [tmp0]"=&r"(tmp[0]),
          [pixels]"+&r"(pixels),            [block]"+&r"(block)
        : [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}
