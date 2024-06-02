/*
 * Loongson SIMD optimized mpegvideo
 *
 * Copyright (c) 2015 Loongson Technology Corporation Limited
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
 *                    Zhang Shuangshuang <zhangshuangshuang@ict.ac.cn>
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

#include "mpegvideo_mips.h"
#include "libavutil/mips/mmiutils.h"

void ff_denoise_dct_mmi(MpegEncContext *s, int16_t *block)
{
    const int intra = s->mb_intra;
    int *sum = s->dct_error_sum[intra];
    uint16_t *offset = s->dct_offset[intra];
    double ftmp[8];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;

    s->dct_count[intra]++;

    __asm__ volatile(
        "pxor       %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "1:                                                             \n\t"
        MMI_LDC1(%[ftmp1], %[block], 0x00)
        "pxor       %[ftmp2],   %[ftmp2],       %[ftmp2]                \n\t"
        MMI_LDC1(%[ftmp3], %[block], 0x08)
        "pxor       %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        "pcmpgth    %[ftmp2],   %[ftmp2],       %[ftmp1]                \n\t"
        "pcmpgth    %[ftmp4],   %[ftmp4],       %[ftmp3]                \n\t"
        "pxor       %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "pxor       %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        MMI_LDC1(%[ftmp6], %[offset], 0x00)
        "mov.d      %[ftmp5],   %[ftmp1]                                \n\t"
        "psubush    %[ftmp1],   %[ftmp1],       %[ftmp6]                \n\t"
        MMI_LDC1(%[ftmp6], %[offset], 0x08)
        "mov.d      %[ftmp7],   %[ftmp3]                                \n\t"
        "psubush    %[ftmp3],   %[ftmp3],       %[ftmp6]                \n\t"
        "pxor       %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "pxor       %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        MMI_SDC1(%[ftmp1], %[block], 0x00)
        MMI_SDC1(%[ftmp3], %[block], 0x08)
        "mov.d      %[ftmp1],   %[ftmp5]                                \n\t"
        "mov.d      %[ftmp3],   %[ftmp7]                                \n\t"
        "punpcklhw  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklhw  %[ftmp7],   %[ftmp7],       %[ftmp0]                \n\t"
        "punpckhhw  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        MMI_LDC1(%[ftmp2], %[sum], 0x00)
        "paddw      %[ftmp5],   %[ftmp5],       %[ftmp2]                \n\t"
        MMI_LDC1(%[ftmp2], %[sum], 0x08)
        "paddw      %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        MMI_LDC1(%[ftmp2], %[sum], 0x10)
        "paddw      %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
        MMI_LDC1(%[ftmp2], %[sum], 0x18)
        "paddw      %[ftmp3],   %[ftmp3],       %[ftmp2]                \n\t"
        MMI_SDC1(%[ftmp5], %[sum], 0x00)
        MMI_SDC1(%[ftmp1], %[sum], 0x08)
        MMI_SDC1(%[ftmp7], %[sum], 0x10)
        MMI_SDC1(%[ftmp3], %[sum], 0x18)
        PTR_ADDIU  "%[block],   %[block],       0x10                    \n\t"
        PTR_ADDIU  "%[sum],     %[sum],         0x20                    \n\t"
        PTR_SUBU   "%[addr0],   %[block1],      %[block]                \n\t"
        PTR_ADDIU  "%[offset],  %[offset],      0x10                    \n\t"
        "bgtz       %[addr0],   1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          RESTRICT_ASM_ALL64
          [addr0]"=&r"(addr[0]),
          [block]"+&r"(block),              [sum]"+&r"(sum),
          [offset]"+&r"(offset)
        : [block1]"r"(block+64)
        : "memory"
    );
}
