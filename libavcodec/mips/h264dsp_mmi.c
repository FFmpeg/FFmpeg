/*
 * Loongson SIMD optimized h264dsp
 *
 * Copyright (c) 2015 Loongson Technology Corporation Limited
 * Copyright (c) 2015 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
 *                    Zhang Shuangshuang <zhangshuangshuang@ict.ac.cn>
 *                    Heiher <r@hev.cc>
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

#include "libavcodec/bit_depth_template.c"
#include "h264dsp_mips.h"
#include "libavutil/mips/asmdefs.h"

void ff_h264_add_pixels4_8_mmi(uint8_t *dst, int16_t *src, int stride)
{
    double ftmp[9];
    uint64_t low32;

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "ldc1       %[ftmp1],   0x00(%[src])                            \n\t"
        "ldc1       %[ftmp2],   0x08(%[src])                            \n\t"
        "ldc1       %[ftmp3],   0x10(%[src])                            \n\t"
        "ldc1       %[ftmp4],   0x18(%[src])                            \n\t"
        "uld        %[low32],   0x00(%[dst0])                           \n\t"
        "mtc1       %[low32],   %[ftmp5]                                \n\t"
        "uld        %[low32],   0x00(%[dst1])                           \n\t"
        "mtc1       %[low32],   %[ftmp6]                                \n\t"
        "uld        %[low32],   0x00(%[dst2])                           \n\t"
        "mtc1       %[low32],   %[ftmp7]                                \n\t"
        "uld        %[low32],   0x00(%[dst3])                           \n\t"
        "mtc1       %[low32],   %[ftmp8]                                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp7],   %[ftmp7],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp8],   %[ftmp8],       %[ftmp0]                \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "paddh      %[ftmp4],   %[ftmp4],       %[ftmp8]                \n\t"
        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "packushb   %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "gsswlc1    %[ftmp1],   0x03(%[dst0])                           \n\t"
        "gsswrc1    %[ftmp1],   0x00(%[dst0])                           \n\t"
        "gsswlc1    %[ftmp2],   0x03(%[dst1])                           \n\t"
        "gsswrc1    %[ftmp2],   0x00(%[dst1])                           \n\t"
        "gsswlc1    %[ftmp3],   0x03(%[dst2])                           \n\t"
        "gsswrc1    %[ftmp3],   0x00(%[dst2])                           \n\t"
        "gsswlc1    %[ftmp4],   0x03(%[dst3])                           \n\t"
        "gsswrc1    %[ftmp4],   0x00(%[dst3])                           \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [low32]"=&r"(low32)
        : [dst0]"r"(dst),                   [dst1]"r"(dst+stride),
          [dst2]"r"(dst+2*stride),          [dst3]"r"(dst+3*stride),
          [src]"r"(src)
        : "memory"
    );

    memset(src, 0, 32);
}

void ff_h264_idct_add_8_mmi(uint8_t *dst, int16_t *block, int stride)
{
    double ftmp[12];
    uint64_t tmp[1];
    uint64_t low32;

    __asm__ volatile (
        "dli        %[tmp0],    0x01                                    \n\t"
        "ldc1       %[ftmp0],   0x00(%[block])                          \n\t"
        "mtc1       %[tmp0],    %[ftmp8]                                \n\t"
        "ldc1       %[ftmp1],   0x08(%[block])                          \n\t"
        "dli        %[tmp0],    0x06                                    \n\t"
        "ldc1       %[ftmp2],   0x10(%[block])                          \n\t"
        "mtc1       %[tmp0],    %[ftmp9]                                \n\t"
        "psrah      %[ftmp4],   %[ftmp1],       %[ftmp8]                \n\t"
        "ldc1       %[ftmp3],   0x18(%[block])                          \n\t"
        "psrah      %[ftmp5],   %[ftmp3],       %[ftmp8]                \n\t"
        "psubh      %[ftmp4],   %[ftmp4],       %[ftmp3]                \n\t"
        "paddh      %[ftmp5],   %[ftmp5],       %[ftmp1]                \n\t"
        "paddh      %[ftmp10],  %[ftmp2],       %[ftmp0]                \n\t"
        "psubh      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "paddh      %[ftmp11],  %[ftmp5],       %[ftmp10]               \n\t"
        "psubh      %[ftmp2],   %[ftmp10],      %[ftmp5]                \n\t"
        "paddh      %[ftmp10],  %[ftmp4],       %[ftmp0]                \n\t"
        "psubh      %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp11],      %[ftmp10]               \n\t"
        "punpcklhw  %[ftmp5],   %[ftmp11],      %[ftmp10]               \n\t"
        "punpckhhw  %[ftmp4],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhwd  %[ftmp2],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpcklwd  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpcklwd  %[ftmp10],  %[ftmp1],       %[ftmp4]                \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp1],       %[ftmp4]                \n\t"
        "paddh      %[ftmp5],   %[ftmp5],       %[ff_pw_32]             \n\t"
        "psrah      %[ftmp4],   %[ftmp2],       %[ftmp8]                \n\t"
        "psrah      %[ftmp3],   %[ftmp0],       %[ftmp8]                \n\t"
        "psubh      %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]                \n\t"
        "paddh      %[ftmp1],   %[ftmp10],      %[ftmp5]                \n\t"
        "psubh      %[ftmp5],   %[ftmp5],       %[ftmp10]               \n\t"
        "paddh      %[ftmp10],  %[ftmp3],       %[ftmp1]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "paddh      %[ftmp11],  %[ftmp4],       %[ftmp5]                \n\t"
        "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "psubh      %[ftmp5],   %[ftmp5],       %[ftmp4]                \n\t"
        "sdc1       %[ftmp7],   0x00(%[block])                          \n\t"
        "sdc1       %[ftmp7],   0x08(%[block])                          \n\t"
        "sdc1       %[ftmp7],   0x10(%[block])                          \n\t"
        "sdc1       %[ftmp7],   0x18(%[block])                          \n\t"
        "uld        %[low32],   0x00(%[dst])                            \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        "psrah      %[ftmp3],   %[ftmp10],      %[ftmp9]                \n\t"
        "gslwxc1    %[ftmp0],   0x00(%[dst],    %[stride])              \n\t"
        "psrah      %[ftmp4],   %[ftmp11],      %[ftmp9]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "paddh      %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "packushb   %[ftmp0],   %[ftmp0],       %[ftmp7]                \n\t"
        "gsswlc1    %[ftmp2],   0x03(%[dst])                            \n\t"
        "gsswrc1    %[ftmp2],   0x00(%[dst])                            \n\t"
        "gsswxc1    %[ftmp0],   0x00(%[dst],    %[stride])              \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[stride]               \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[stride]               \n\t"
        "uld        %[low32],   0x00(%[dst])                            \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        "psrah      %[ftmp5],   %[ftmp5],       %[ftmp9]                \n\t"
        "gslwxc1    %[ftmp0],   0x00(%[dst],    %[stride])              \n\t"
        "psrah      %[ftmp1],   %[ftmp1],       %[ftmp9]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp7]                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "paddh      %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "gsswlc1    %[ftmp2],   0x03(%[dst])                            \n\t"
        "gsswrc1    %[ftmp2],   0x00(%[dst])                            \n\t"
        "packushb   %[ftmp0],   %[ftmp0],       %[ftmp7]                \n\t"
        "gsswxc1    %[ftmp0],   0x00(%[dst],    %[stride])              \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
          [tmp0]"=&r"(tmp[0]),
          [low32]"=&r"(low32)
        : [dst]"r"(dst),                    [block]"r"(block),
          [stride]"r"((mips_reg)stride),    [ff_pw_32]"f"(ff_pw_32)
        : "memory"
    );

    memset(block, 0, 32);
}

void ff_h264_idct8_add_8_mmi(uint8_t *dst, int16_t *block, int stride)
{
    double ftmp[16];
    uint64_t tmp[8];
    mips_reg addr[1];
    uint64_t low32;

    __asm__ volatile (
        "lhu       %[tmp0],     0x00(%[block])                          \n\t"
        PTR_ADDI  "$29,         $29,            -0x20                   \n\t"
        PTR_ADDIU "%[tmp0],     %[tmp0],        0x20                    \n\t"
        "ldc1      %[ftmp1],    0x10(%[block])                          \n\t"
        "sh        %[tmp0],     0x00(%[block])                          \n\t"
        "ldc1      %[ftmp2],    0x20(%[block])                          \n\t"
        "dli       %[tmp0],     0x01                                    \n\t"
        "ldc1      %[ftmp3],    0x30(%[block])                          \n\t"
        "mtc1      %[tmp0],     %[ftmp8]                                \n\t"
        "ldc1      %[ftmp5],    0x50(%[block])                          \n\t"
        "ldc1      %[ftmp6],    0x60(%[block])                          \n\t"
        "ldc1      %[ftmp7],    0x70(%[block])                          \n\t"
        "mov.d     %[ftmp0],    %[ftmp1]                                \n\t"
        "psrah     %[ftmp1],    %[ftmp1],       %[ftmp8]                \n\t"
        "psrah     %[ftmp4],    %[ftmp5],       %[ftmp8]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp0]                \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp5]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp5]                \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp7]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp3]                \n\t"
        "psubh     %[ftmp4],    %[ftmp4],       %[ftmp0]                \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp3]                \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp3]                \n\t"
        "psrah     %[ftmp3],    %[ftmp3],       %[ftmp8]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp7]                \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp7]                \n\t"
        "psrah     %[ftmp7],    %[ftmp7],       %[ftmp8]                \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp3]                \n\t"
        "dli       %[tmp0],     0x02                                    \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp7]                \n\t"
        "mtc1      %[tmp0],     %[ftmp9]                                \n\t"
        "mov.d     %[ftmp7],    %[ftmp1]                                \n\t"
        "psrah     %[ftmp1],    %[ftmp1],       %[ftmp9]                \n\t"
        "psrah     %[ftmp3],    %[ftmp4],       %[ftmp9]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp0]                \n\t"
        "psrah     %[ftmp0],    %[ftmp0],       %[ftmp9]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp5]                \n\t"
        "psrah     %[ftmp5],    %[ftmp5],       %[ftmp9]                \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp4]                \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp5]                \n\t"
        "mov.d     %[ftmp5],    %[ftmp6]                                \n\t"
        "psrah     %[ftmp6],    %[ftmp6],       %[ftmp8]                \n\t"
        "psrah     %[ftmp4],    %[ftmp2],       %[ftmp8]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp2]                \n\t"
        "psubh     %[ftmp4],    %[ftmp4],       %[ftmp5]                \n\t"
        "ldc1      %[ftmp2],    0x00(%[block])                          \n\t"
        "ldc1      %[ftmp5],    0x40(%[block])                          \n\t"
        "paddh     %[ftmp5],    %[ftmp5],       %[ftmp2]                \n\t"
        "paddh     %[ftmp2],    %[ftmp2],       %[ftmp2]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp5]                \n\t"
        "psubh     %[ftmp2],    %[ftmp2],       %[ftmp5]                \n\t"
        "paddh     %[ftmp5],    %[ftmp5],       %[ftmp5]                \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp2]                \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp6]                \n\t"
        "paddh     %[ftmp2],    %[ftmp2],       %[ftmp2]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp6]                \n\t"
        "psubh     %[ftmp2],    %[ftmp2],       %[ftmp4]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp6]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp4]                \n\t"
        "psubh     %[ftmp6],    %[ftmp6],       %[ftmp7]                \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp4]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp2]                \n\t"
        "psubh     %[ftmp4],    %[ftmp4],       %[ftmp0]                \n\t"
        "paddh     %[ftmp2],    %[ftmp2],       %[ftmp2]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp5]                \n\t"
        "psubh     %[ftmp2],    %[ftmp2],       %[ftmp3]                \n\t"
        "paddh     %[ftmp5],    %[ftmp5],       %[ftmp5]                \n\t"
        "sdc1      %[ftmp6],    0x00(%[block])                          \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp1]                \n\t"
        "punpckhhw %[ftmp6],    %[ftmp7],       %[ftmp0]                \n\t"
        "punpcklhw %[ftmp7],    %[ftmp7],       %[ftmp0]                \n\t"
        "punpckhhw %[ftmp0],    %[ftmp3],       %[ftmp1]                \n\t"
        "punpcklhw %[ftmp3],    %[ftmp3],       %[ftmp1]                \n\t"
        "punpckhwd %[ftmp1],    %[ftmp7],       %[ftmp3]                \n\t"
        "punpcklwd %[ftmp7],    %[ftmp7],       %[ftmp3]                \n\t"
        "punpckhwd %[ftmp3],    %[ftmp6],       %[ftmp0]                \n\t"
        "punpcklwd %[ftmp6],    %[ftmp6],       %[ftmp0]                \n\t"
        "ldc1      %[ftmp0],    0x00(%[block])                          \n\t"
        "sdc1      %[ftmp7],    0x00($29)                               \n\t"
        "sdc1      %[ftmp1],    0x10($29)                               \n\t"
        "dmfc1     %[tmp1],     %[ftmp6]                                \n\t"
        "dmfc1     %[tmp3],     %[ftmp3]                                \n\t"
        "punpckhhw %[ftmp3],    %[ftmp5],       %[ftmp2]                \n\t"
        "punpcklhw %[ftmp5],    %[ftmp5],       %[ftmp2]                \n\t"
        "punpckhhw %[ftmp2],    %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklhw %[ftmp4],    %[ftmp4],       %[ftmp0]                \n\t"
        "punpckhwd %[ftmp0],    %[ftmp5],       %[ftmp4]                \n\t"
        "punpcklwd %[ftmp5],    %[ftmp5],       %[ftmp4]                \n\t"
        "punpckhwd %[ftmp4],    %[ftmp3],       %[ftmp2]                \n\t"
        "punpcklwd %[ftmp3],    %[ftmp3],       %[ftmp2]                \n\t"
        "sdc1      %[ftmp5],    0x08($29)                               \n\t"
        "sdc1      %[ftmp0],    0x18($29)                               \n\t"
        "dmfc1     %[tmp2],     %[ftmp3]                                \n\t"
        "dmfc1     %[tmp4],     %[ftmp4]                                \n\t"
        "ldc1      %[ftmp1],    0x18(%[block])                          \n\t"
        "ldc1      %[ftmp6],    0x28(%[block])                          \n\t"
        "ldc1      %[ftmp2],    0x38(%[block])                          \n\t"
        "ldc1      %[ftmp0],    0x58(%[block])                          \n\t"
        "ldc1      %[ftmp3],    0x68(%[block])                          \n\t"
        "ldc1      %[ftmp4],    0x78(%[block])                          \n\t"
        "mov.d     %[ftmp7],    %[ftmp1]                                \n\t"
        "psrah     %[ftmp5],    %[ftmp0],       %[ftmp8]                \n\t"
        "psrah     %[ftmp1],    %[ftmp1],       %[ftmp8]                \n\t"
        "paddh     %[ftmp5],    %[ftmp5],       %[ftmp0]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp7]                \n\t"
        "paddh     %[ftmp5],    %[ftmp5],       %[ftmp4]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp0]                \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp7]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp2]                \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp2]                \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp2]                \n\t"
        "psrah     %[ftmp2],    %[ftmp2],       %[ftmp8]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp4]                \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp4]                \n\t"
        "psrah     %[ftmp4],    %[ftmp4],       %[ftmp8]                \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp2]                \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp4]                \n\t"
        "mov.d     %[ftmp4],    %[ftmp1]                                \n\t"
        "psrah     %[ftmp2],    %[ftmp5],       %[ftmp9]                \n\t"
        "psrah     %[ftmp1],    %[ftmp1],       %[ftmp9]                \n\t"
        "paddh     %[ftmp2],    %[ftmp2],       %[ftmp7]                \n\t"
        "psrah     %[ftmp7],    %[ftmp7],       %[ftmp9]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp0]                \n\t"
        "psrah     %[ftmp0],    %[ftmp0],       %[ftmp9]                \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp5]                \n\t"
        "psubh     %[ftmp4],    %[ftmp4],       %[ftmp0]                \n\t"
        "mov.d     %[ftmp0],    %[ftmp3]                                \n\t"
        "psrah     %[ftmp3],    %[ftmp3],       %[ftmp8]                \n\t"
        "psrah     %[ftmp5],    %[ftmp6],       %[ftmp8]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp6]                \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp0]                \n\t"
        "ldc1      %[ftmp6],    0x08(%[block])                          \n\t"
        "ldc1      %[ftmp0],    0x48(%[block])                          \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp6]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp6]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp0]                \n\t"
        "psubh     %[ftmp6],    %[ftmp6],       %[ftmp0]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp0]                \n\t"
        "paddh     %[ftmp5],    %[ftmp5],       %[ftmp6]                \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp3]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp6]                \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp3]                \n\t"
        "psubh     %[ftmp6],    %[ftmp6],       %[ftmp5]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp3]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp5]                \n\t"
        "psubh     %[ftmp3],    %[ftmp3],       %[ftmp4]                \n\t"
        "paddh     %[ftmp5],    %[ftmp5],       %[ftmp5]                \n\t"
        "paddh     %[ftmp2],    %[ftmp2],       %[ftmp6]                \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp7]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp6]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp0]                \n\t"
        "psubh     %[ftmp6],    %[ftmp6],       %[ftmp2]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp0]                \n\t"
        "sdc1      %[ftmp3],    0x08(%[block])                          \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp1]                \n\t"
        "punpckhhw %[ftmp3],    %[ftmp4],       %[ftmp7]                \n\t"
        "punpcklhw %[ftmp4],    %[ftmp4],       %[ftmp7]                \n\t"
        "punpckhhw %[ftmp7],    %[ftmp2],       %[ftmp1]                \n\t"
        "punpcklhw %[ftmp2],    %[ftmp2],       %[ftmp1]                \n\t"
        "punpckhwd %[ftmp1],    %[ftmp4],       %[ftmp2]                \n\t"
        "punpcklwd %[ftmp4],    %[ftmp4],       %[ftmp2]                \n\t"
        "punpckhwd %[ftmp2],    %[ftmp3],       %[ftmp7]                \n\t"
        "punpcklwd %[ftmp3],    %[ftmp3],       %[ftmp7]                \n\t"
        "ldc1      %[ftmp7],    0x08(%[block])                          \n\t"
        "dmfc1     %[tmp5],     %[ftmp4]                                \n\t"
        "dmfc1     %[tmp7],     %[ftmp1]                                \n\t"
        "mov.d     %[ftmp12],   %[ftmp3]                                \n\t"
        "mov.d     %[ftmp14],   %[ftmp2]                                \n\t"
        "punpckhhw %[ftmp2],    %[ftmp0],       %[ftmp6]                \n\t"
        "punpcklhw %[ftmp0],    %[ftmp0],       %[ftmp6]                \n\t"
        "punpckhhw %[ftmp6],    %[ftmp5],       %[ftmp7]                \n\t"
        "punpcklhw %[ftmp5],    %[ftmp5],       %[ftmp7]                \n\t"
        "punpckhwd %[ftmp7],    %[ftmp0],       %[ftmp5]                \n\t"
        "punpcklwd %[ftmp0],    %[ftmp0],       %[ftmp5]                \n\t"
        "punpckhwd %[ftmp5],    %[ftmp2],       %[ftmp6]                \n\t"
        "punpcklwd %[ftmp2],    %[ftmp2],       %[ftmp6]                \n\t"
        "dmfc1     %[tmp6],     %[ftmp0]                                \n\t"
        "mov.d     %[ftmp11],   %[ftmp7]                                \n\t"
        "mov.d     %[ftmp13],   %[ftmp2]                                \n\t"
        "mov.d     %[ftmp15],   %[ftmp5]                                \n\t"
        PTR_ADDIU "%[addr0],    %[dst],         0x04                    \n\t"
        "dmtc1     %[tmp7],     %[ftmp7]                                \n\t"
        "dmtc1     %[tmp3],     %[ftmp6]                                \n\t"
        "ldc1      %[ftmp1],    0x10($29)                               \n\t"
        "dmtc1     %[tmp1],     %[ftmp3]                                \n\t"
        "mov.d     %[ftmp4],    %[ftmp1]                                \n\t"
        "psrah     %[ftmp1],    %[ftmp1],       %[ftmp8]                \n\t"
        "psrah     %[ftmp0],    %[ftmp7],       %[ftmp8]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp4]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp7]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp7]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp14]               \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp6]                \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp4]                \n\t"
        "psubh     %[ftmp4],    %[ftmp4],       %[ftmp6]                \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp6]                \n\t"
        "psrah     %[ftmp6],    %[ftmp6],       %[ftmp8]                \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp14]               \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp14]               \n\t"
        "psrah     %[ftmp5],    %[ftmp14],      %[ftmp8]                \n\t"
        "psubh     %[ftmp4],    %[ftmp4],       %[ftmp6]                \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp5]                \n\t"
        "mov.d     %[ftmp5],    %[ftmp1]                                \n\t"
        "psrah     %[ftmp1],    %[ftmp1],       %[ftmp9]                \n\t"
        "psrah     %[ftmp6],    %[ftmp0],       %[ftmp9]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp7]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp4]                \n\t"
        "psrah     %[ftmp4],    %[ftmp4],       %[ftmp9]                \n\t"
        "psrah     %[ftmp7],    %[ftmp7],       %[ftmp9]                \n\t"
        "psubh     %[ftmp4],    %[ftmp4],       %[ftmp0]                \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp7]                \n\t"
        "mov.d     %[ftmp7],    %[ftmp12]                               \n\t"
        "psrah     %[ftmp2],    %[ftmp12],      %[ftmp8]                \n\t"
        "psrah     %[ftmp0],    %[ftmp3],       %[ftmp8]                \n\t"
        "paddh     %[ftmp2],    %[ftmp2],       %[ftmp3]                \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp7]                \n\t"
        "ldc1      %[ftmp3],    0x00($29)                               \n\t"
        "dmtc1     %[tmp5],     %[ftmp7]                                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp3]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp3]                \n\t"
        "paddh     %[ftmp2],    %[ftmp2],       %[ftmp7]                \n\t"
        "psubh     %[ftmp3],    %[ftmp3],       %[ftmp7]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp7]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp3]                \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp2]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp3]                \n\t"
        "paddh     %[ftmp5],    %[ftmp5],       %[ftmp2]                \n\t"
        "psubh     %[ftmp3],    %[ftmp3],       %[ftmp0]                \n\t"
        "paddh     %[ftmp2],    %[ftmp2],       %[ftmp2]                \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp0]                \n\t"
        "psubh     %[ftmp2],    %[ftmp2],       %[ftmp5]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp0]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp3]                \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp4]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp3]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp7]                \n\t"
        "psubh     %[ftmp3],    %[ftmp3],       %[ftmp6]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp7]                \n\t"
        "sdc1      %[ftmp3],    0x00($29)                               \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp1]                \n\t"
        "sdc1      %[ftmp0],    0x10($29)                               \n\t"
        "dmfc1     %[tmp1],     %[ftmp2]                                \n\t"
        "xor       %[ftmp2],    %[ftmp2],       %[ftmp2]                \n\t"
        "sdc1      %[ftmp2],    0x00(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x08(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x10(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x18(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x20(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x28(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x30(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x38(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x40(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x48(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x50(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x58(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x60(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x68(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x70(%[block])                          \n\t"
        "sdc1      %[ftmp2],    0x78(%[block])                          \n\t"
        "dli       %[tmp3],     0x06                                    \n\t"
        "uld       %[low32],    0x00(%[dst])                            \n\t"
        "mtc1      %[low32],    %[ftmp3]                                \n\t"
        "mtc1      %[tmp3],     %[ftmp10]                               \n\t"
        "gslwxc1   %[ftmp0],    0x00(%[dst],    %[stride])              \n\t"
        "psrah     %[ftmp5],    %[ftmp5],       %[ftmp10]               \n\t"
        "psrah     %[ftmp4],    %[ftmp4],       %[ftmp10]               \n\t"
        "punpcklbh %[ftmp3],    %[ftmp3],       %[ftmp2]                \n\t"
        "punpcklbh %[ftmp0],    %[ftmp0],       %[ftmp2]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp5]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp4]                \n\t"
        "packushb  %[ftmp3],    %[ftmp3],       %[ftmp2]                \n\t"
        "packushb  %[ftmp0],    %[ftmp0],       %[ftmp2]                \n\t"
        "gsswlc1   %[ftmp3],    0x03(%[dst])                            \n\t"
        "gsswrc1   %[ftmp3],    0x00(%[dst])                            \n\t"
        "gsswxc1   %[ftmp0],    0x00(%[dst],    %[stride])              \n\t"
        PTR_ADDU  "%[dst],      %[dst],         %[stride]               \n\t"
        PTR_ADDU  "%[dst],      %[dst],         %[stride]               \n\t"
        "uld       %[low32],    0x00(%[dst])                            \n\t"
        "mtc1      %[low32],    %[ftmp3]                                \n\t"
        "gslwxc1   %[ftmp0],    0x00(%[dst],    %[stride])              \n\t"
        "psrah     %[ftmp6],    %[ftmp6],       %[ftmp10]               \n\t"
        "psrah     %[ftmp1],    %[ftmp1],       %[ftmp10]               \n\t"
        "punpcklbh %[ftmp3],    %[ftmp3],       %[ftmp2]                \n\t"
        "punpcklbh %[ftmp0],    %[ftmp0],       %[ftmp2]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp6]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp1]                \n\t"
        "packushb  %[ftmp3],    %[ftmp3],       %[ftmp2]                \n\t"
        "packushb  %[ftmp0],    %[ftmp0],       %[ftmp2]                \n\t"
        "gsswlc1   %[ftmp3],    0x03(%[dst])                            \n\t"
        "gsswrc1   %[ftmp3],    0x00(%[dst])                            \n\t"
        "gsswxc1   %[ftmp0],    0x00(%[dst],    %[stride])              \n\t"
        "ldc1      %[ftmp5],    0x00($29)                               \n\t"
        "ldc1      %[ftmp4],    0x10($29)                               \n\t"
        "dmtc1     %[tmp1],     %[ftmp6]                                \n\t"
        PTR_ADDU  "%[dst],      %[dst],         %[stride]               \n\t"
        PTR_ADDU  "%[dst],      %[dst],         %[stride]               \n\t"
        "uld       %[low32],    0x00(%[dst])                            \n\t"
        "mtc1      %[low32],    %[ftmp3]                                \n\t"
        "gslwxc1   %[ftmp0],    0x00(%[dst],    %[stride])              \n\t"
        "psrah     %[ftmp7],    %[ftmp7],       %[ftmp10]               \n\t"
        "psrah     %[ftmp5],    %[ftmp5],       %[ftmp10]               \n\t"
        "punpcklbh %[ftmp3],    %[ftmp3],       %[ftmp2]                \n\t"
        "punpcklbh %[ftmp0],    %[ftmp0],       %[ftmp2]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp7]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp5]                \n\t"
        "packushb  %[ftmp3],    %[ftmp3],       %[ftmp2]                \n\t"
        "packushb  %[ftmp0],    %[ftmp0],       %[ftmp2]                \n\t"
        "gsswlc1   %[ftmp3],    0x03(%[dst])                            \n\t"
        "gsswrc1   %[ftmp3],    0x00(%[dst])                            \n\t"
        "gsswxc1   %[ftmp0],    0x00(%[dst],    %[stride])              \n\t"
        PTR_ADDU  "%[dst],      %[dst],         %[stride]               \n\t"
        PTR_ADDU  "%[dst],      %[dst],         %[stride]               \n\t"
        "uld       %[low32],    0x00(%[dst])                            \n\t"
        "mtc1      %[low32],    %[ftmp3]                                \n\t"
        "gslwxc1   %[ftmp0],    0x00(%[dst],    %[stride])              \n\t"
        "psrah     %[ftmp4],    %[ftmp4],       %[ftmp10]               \n\t"
        "psrah     %[ftmp6],    %[ftmp6],       %[ftmp10]               \n\t"
        "punpcklbh %[ftmp3],    %[ftmp3],       %[ftmp2]                \n\t"
        "punpcklbh %[ftmp0],    %[ftmp0],       %[ftmp2]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp4]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp6]                \n\t"
        "packushb  %[ftmp3],    %[ftmp3],       %[ftmp2]                \n\t"
        "packushb  %[ftmp0],    %[ftmp0],       %[ftmp2]                \n\t"
        "gsswlc1   %[ftmp3],    0x03(%[dst])                            \n\t"
        "gsswrc1   %[ftmp3],    0x00(%[dst])                            \n\t"
        "gsswxc1   %[ftmp0],    0x00(%[dst],    %[stride])              \n\t"
        "dmtc1     %[tmp4],     %[ftmp1]                                \n\t"
        "dmtc1     %[tmp2],     %[ftmp6]                                \n\t"
        "ldc1      %[ftmp4],    0x18($29)                               \n\t"
        "mov.d     %[ftmp5],    %[ftmp4]                                \n\t"
        "psrah     %[ftmp4],    %[ftmp4],       %[ftmp8]                \n\t"
        "psrah     %[ftmp7],    %[ftmp11],      %[ftmp8]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp11]               \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp5]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp15]               \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp11]               \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp5]                \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp1]                \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp1]                \n\t"
        "psubh     %[ftmp3],    %[ftmp11],      %[ftmp1]                \n\t"
        "psrah     %[ftmp1],    %[ftmp1],       %[ftmp8]                \n\t"
        "paddh     %[ftmp5],    %[ftmp5],       %[ftmp15]               \n\t"
        "psubh     %[ftmp3],    %[ftmp3],       %[ftmp15]               \n\t"
        "psrah     %[ftmp2],    %[ftmp15],      %[ftmp8]                \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp1]                \n\t"
        "psubh     %[ftmp3],    %[ftmp3],       %[ftmp2]                \n\t"
        "mov.d     %[ftmp2],    %[ftmp4]                                \n\t"
        "psrah     %[ftmp4],    %[ftmp4],       %[ftmp9]                \n\t"
        "psrah     %[ftmp1],    %[ftmp7],       %[ftmp9]                \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp3]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp5]                \n\t"
        "psrah     %[ftmp5],    %[ftmp5],       %[ftmp9]                \n\t"
        "psrah     %[ftmp3],    %[ftmp3],       %[ftmp9]                \n\t"
        "psubh     %[ftmp5],    %[ftmp5],       %[ftmp7]                \n\t"
        "psubh     %[ftmp2],    %[ftmp2],       %[ftmp3]                \n\t"
        "mov.d     %[ftmp3],    %[ftmp13]                               \n\t"
        "psrah     %[ftmp0],    %[ftmp13],      %[ftmp8]                \n\t"
        "psrah     %[ftmp7],    %[ftmp6],       %[ftmp8]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp6]                \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp3]                \n\t"
        "ldc1      %[ftmp6],    0x08($29)                               \n\t"
        "dmtc1     %[tmp6],     %[ftmp3]                                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp6]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp6]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp3]                \n\t"
        "psubh     %[ftmp6],    %[ftmp6],       %[ftmp3]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp3]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp6]                \n\t"
        "psubh     %[ftmp3],    %[ftmp3],       %[ftmp0]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp6]                \n\t"
        "paddh     %[ftmp2],    %[ftmp2],       %[ftmp0]                \n\t"
        "psubh     %[ftmp6],    %[ftmp6],       %[ftmp7]                \n\t"
        "paddh     %[ftmp0],    %[ftmp0],       %[ftmp0]                \n\t"
        "paddh     %[ftmp5],    %[ftmp5],       %[ftmp7]                \n\t"
        "psubh     %[ftmp0],    %[ftmp0],       %[ftmp2]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp7]                \n\t"
        "paddh     %[ftmp1],    %[ftmp1],       %[ftmp6]                \n\t"
        "psubh     %[ftmp7],    %[ftmp7],       %[ftmp5]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp6]                \n\t"
        "paddh     %[ftmp4],    %[ftmp4],       %[ftmp3]                \n\t"
        "psubh     %[ftmp6],    %[ftmp6],       %[ftmp1]                \n\t"
        "paddh     %[ftmp3],    %[ftmp3],       %[ftmp3]                \n\t"
        "sdc1      %[ftmp6],    0x08($29)                               \n\t"
        "psubh     %[ftmp3],    %[ftmp3],       %[ftmp4]                \n\t"
        "sdc1      %[ftmp7],    0x18($29)                               \n\t"
        "dmfc1     %[tmp2],     %[ftmp0]                                \n\t"
        "xor       %[ftmp0],    %[ftmp0],       %[ftmp0]                \n\t"
        "uld       %[low32],    0x00(%[addr0])                          \n\t"
        "mtc1      %[low32],    %[ftmp6]                                \n\t"
        "gslwxc1   %[ftmp7],    0x00(%[addr0],  %[stride])              \n\t"
        "psrah     %[ftmp2],    %[ftmp2],       %[ftmp10]               \n\t"
        "psrah     %[ftmp5],    %[ftmp5],       %[ftmp10]               \n\t"
        "punpcklbh %[ftmp6],    %[ftmp6],       %[ftmp0]                \n\t"
        "punpcklbh %[ftmp7],    %[ftmp7],       %[ftmp0]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp2]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp5]                \n\t"
        "packushb  %[ftmp6],    %[ftmp6],       %[ftmp0]                \n\t"
        "packushb  %[ftmp7],    %[ftmp7],       %[ftmp0]                \n\t"
        "gsswlc1   %[ftmp6],    0x03(%[addr0])                          \n\t"
        "gsswrc1   %[ftmp6],    0x00(%[addr0])                          \n\t"
        "gsswxc1   %[ftmp7],    0x00(%[addr0],  %[stride])              \n\t"
        PTR_ADDU  "%[addr0],    %[addr0],       %[stride]               \n\t"
        PTR_ADDU  "%[addr0],    %[addr0],       %[stride]               \n\t"
        "uld       %[low32],    0x00(%[addr0])                          \n\t"
        "mtc1      %[low32],    %[ftmp6]                                \n\t"
        "gslwxc1   %[ftmp7],    0x00(%[addr0],  %[stride])              \n\t"
        "psrah     %[ftmp1],    %[ftmp1],       %[ftmp10]               \n\t"
        "psrah     %[ftmp4],    %[ftmp4],       %[ftmp10]               \n\t"
        "punpcklbh %[ftmp6],    %[ftmp6],       %[ftmp0]                \n\t"
        "punpcklbh %[ftmp7],    %[ftmp7],       %[ftmp0]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp1]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp4]                \n\t"
        "packushb  %[ftmp6],    %[ftmp6],       %[ftmp0]                \n\t"
        "packushb  %[ftmp7],    %[ftmp7],       %[ftmp0]                \n\t"
        "gsswlc1   %[ftmp6],    0x03(%[addr0])                          \n\t"
        "gsswrc1   %[ftmp6],    0x00(%[addr0])                          \n\t"
        "gsswxc1   %[ftmp7],    0x00(%[addr0],  %[stride])              \n\t"
        "ldc1      %[ftmp2],    0x08($29)                               \n\t"
        "ldc1      %[ftmp5],    0x18($29)                               \n\t"
        PTR_ADDU  "%[addr0],    %[addr0],       %[stride]               \n\t"
        "dmtc1     %[tmp2],     %[ftmp1]                                \n\t"
        PTR_ADDU  "%[addr0],    %[addr0],       %[stride]               \n\t"
        "uld       %[low32],    0x00(%[addr0])                          \n\t"
        "mtc1      %[low32],    %[ftmp6]                                \n\t"
        "gslwxc1   %[ftmp7],    0x00(%[addr0],  %[stride])              \n\t"
        "psrah     %[ftmp3],    %[ftmp3],       %[ftmp10]               \n\t"
        "psrah     %[ftmp2],    %[ftmp2],       %[ftmp10]               \n\t"
        "punpcklbh %[ftmp6],    %[ftmp6],       %[ftmp0]                \n\t"
        "punpcklbh %[ftmp7],    %[ftmp7],       %[ftmp0]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp3]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp2]                \n\t"
        "packushb  %[ftmp6],    %[ftmp6],       %[ftmp0]                \n\t"
        "packushb  %[ftmp7],    %[ftmp7],       %[ftmp0]                \n\t"
        "gsswlc1   %[ftmp6],    0x03(%[addr0])                          \n\t"
        "gsswrc1   %[ftmp6],    0x00(%[addr0])                          \n\t"
        "gsswxc1   %[ftmp7],    0x00(%[addr0],  %[stride])              \n\t"
        PTR_ADDU  "%[addr0],    %[addr0],       %[stride]               \n\t"
        PTR_ADDU  "%[addr0],    %[addr0],       %[stride]               \n\t"
        "uld       %[low32],    0x00(%[addr0])                          \n\t"
        "mtc1      %[low32],    %[ftmp6]                                \n\t"
        "gslwxc1   %[ftmp7],    0x00(%[addr0],  %[stride])              \n\t"
        "psrah     %[ftmp5],    %[ftmp5],       %[ftmp10]               \n\t"
        "psrah     %[ftmp1],    %[ftmp1],       %[ftmp10]               \n\t"
        "punpcklbh %[ftmp6],    %[ftmp6],       %[ftmp0]                \n\t"
        "punpcklbh %[ftmp7],    %[ftmp7],       %[ftmp0]                \n\t"
        "paddh     %[ftmp6],    %[ftmp6],       %[ftmp5]                \n\t"
        "paddh     %[ftmp7],    %[ftmp7],       %[ftmp1]                \n\t"
        "packushb  %[ftmp6],    %[ftmp6],       %[ftmp0]                \n\t"
        "packushb  %[ftmp7],    %[ftmp7],       %[ftmp0]                \n\t"
        "gsswlc1   %[ftmp6],    0x03(%[addr0])                          \n\t"
        "gsswrc1   %[ftmp6],    0x00(%[addr0])                          \n\t"
        "gsswxc1   %[ftmp7],    0x00(%[addr0],  %[stride])              \n\t"
        PTR_ADDIU "$29,         $29,            0x20                    \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
          [ftmp12]"=&f"(ftmp[12]),          [ftmp13]"=&f"(ftmp[13]),
          [ftmp14]"=&f"(ftmp[14]),          [ftmp15]"=&f"(ftmp[15]),
          [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          [tmp2]"=&r"(tmp[2]),              [tmp3]"=&r"(tmp[3]),
          [tmp4]"=&r"(tmp[4]),              [tmp5]"=&r"(tmp[5]),
          [tmp6]"=&r"(tmp[6]),              [tmp7]"=&r"(tmp[7]),
          [addr0]"=&r"(addr[0]),
          [low32]"=&r"(low32)
        : [dst]"r"(dst),                    [block]"r"(block),
          [stride]"r"((mips_reg)stride)
        : "$29","memory"
    );

    memset(block, 0, 128);
}

void ff_h264_idct_dc_add_8_mmi(uint8_t *dst, int16_t *block, int stride)
{
    int dc = (block[0] + 32) >> 6;
    double ftmp[6];
    uint64_t low32;

    block[0] = 0;

    __asm__ volatile (
        "mtc1       %[dc],      %[ftmp5]                                \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "uld        %[low32],   0x00(%[dst0])                           \n\t"
        "mtc1       %[low32],   %[ftmp1]                                \n\t"
        "uld        %[low32],   0x00(%[dst1])                           \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        "uld        %[low32],   0x00(%[dst2])                           \n\t"
        "mtc1       %[low32],   %[ftmp3]                                \n\t"
        "uld        %[low32],   0x00(%[dst3])                           \n\t"
        "mtc1       %[low32],   %[ftmp4]                                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "packushb   %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "gsswlc1    %[ftmp1],   0x03(%[dst0])                           \n\t"
        "gsswrc1    %[ftmp1],   0x00(%[dst0])                           \n\t"
        "gsswlc1    %[ftmp2],   0x03(%[dst1])                           \n\t"
        "gsswrc1    %[ftmp2],   0x00(%[dst1])                           \n\t"
        "gsswlc1    %[ftmp3],   0x03(%[dst2])                           \n\t"
        "gsswrc1    %[ftmp3],   0x00(%[dst2])                           \n\t"
        "gsswlc1    %[ftmp4],   0x03(%[dst3])                           \n\t"
        "gsswrc1    %[ftmp4],   0x00(%[dst3])                           \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [low32]"=&r"(low32)
        : [dst0]"r"(dst),                   [dst1]"r"(dst+stride),
          [dst2]"r"(dst+2*stride),          [dst3]"r"(dst+3*stride),
          [dc]"r"(dc)
        : "memory"
    );
}

void ff_h264_idct8_dc_add_8_mmi(uint8_t *dst, int16_t *block, int stride)
{
    int dc = (block[0] + 32) >> 6;
    double ftmp[10];

    block[0] = 0;

    __asm__ volatile (
        "mtc1       %[dc],      %[ftmp5]                                \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "ldc1       %[ftmp1],   0x00(%[dst0])                           \n\t"
        "ldc1       %[ftmp2],   0x00(%[dst1])                           \n\t"
        "ldc1       %[ftmp3],   0x00(%[dst2])                           \n\t"
        "ldc1       %[ftmp4],   0x00(%[dst3])                           \n\t"
        "punpckhbh  %[ftmp6],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp9],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp9],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp6]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "packushb   %[ftmp3],   %[ftmp3],       %[ftmp8]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp9]                \n\t"
        "sdc1       %[ftmp1],   0x00(%[dst0])                           \n\t"
        "sdc1       %[ftmp2],   0x00(%[dst1])                           \n\t"
        "sdc1       %[ftmp3],   0x00(%[dst2])                           \n\t"
        "sdc1       %[ftmp4],   0x00(%[dst3])                           \n\t"

        "ldc1       %[ftmp1],   0x00(%[dst4])                           \n\t"
        "ldc1       %[ftmp2],   0x00(%[dst5])                           \n\t"
        "ldc1       %[ftmp3],   0x00(%[dst6])                           \n\t"
        "ldc1       %[ftmp4],   0x00(%[dst7])                           \n\t"
        "punpckhbh  %[ftmp6],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp9],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp9],       %[ftmp5]                \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp6]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "packushb   %[ftmp3],   %[ftmp3],       %[ftmp8]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp9]                \n\t"
        "sdc1       %[ftmp1],   0x00(%[dst4])                           \n\t"
        "sdc1       %[ftmp2],   0x00(%[dst5])                           \n\t"
        "sdc1       %[ftmp3],   0x00(%[dst6])                           \n\t"
        "sdc1       %[ftmp4],   0x00(%[dst7])                           \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9])
        : [dst0]"r"(dst),                   [dst1]"r"(dst+stride),
          [dst2]"r"(dst+2*stride),          [dst3]"r"(dst+3*stride),
          [dst4]"r"(dst+4*stride),          [dst5]"r"(dst+5*stride),
          [dst6]"r"(dst+6*stride),          [dst7]"r"(dst+7*stride),
          [dc]"r"(dc)
        : "memory"
    );
}

void ff_h264_idct_add16_8_mmi(uint8_t *dst, const int *block_offset,
        int16_t *block, int stride, const uint8_t nnzc[15*8])
{
    int i;
    for(i=0; i<16; i++){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && ((int16_t*)block)[i*16])
                ff_h264_idct_dc_add_8_mmi(dst + block_offset[i], block + i*16,
                        stride);
            else
                ff_h264_idct_add_8_mmi(dst + block_offset[i], block + i*16,
                        stride);
        }
    }
}

void ff_h264_idct_add16intra_8_mmi(uint8_t *dst, const int *block_offset,
        int16_t *block, int stride, const uint8_t nnzc[15*8])
{
    int i;
    for(i=0; i<16; i++){
        if(nnzc[ scan8[i] ])
            ff_h264_idct_add_8_mmi(dst + block_offset[i], block + i*16, stride);
        else if(((int16_t*)block)[i*16])
            ff_h264_idct_dc_add_8_mmi(dst + block_offset[i], block + i*16,
                    stride);
    }
}

void ff_h264_idct8_add4_8_mmi(uint8_t *dst, const int *block_offset,
        int16_t *block, int stride, const uint8_t nnzc[15*8])
{
    int i;
    for(i=0; i<16; i+=4){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && ((int16_t*)block)[i*16])
                ff_h264_idct8_dc_add_8_mmi(dst + block_offset[i],
                        block + i*16, stride);
            else
                ff_h264_idct8_add_8_mmi(dst + block_offset[i], block + i*16,
                        stride);
        }
    }
}

void ff_h264_idct_add8_8_mmi(uint8_t **dest, const int *block_offset,
        int16_t *block, int stride, const uint8_t nnzc[15*8])
{
    int i, j;
    for(j=1; j<3; j++){
        for(i=j*16; i<j*16+4; i++){
            if(nnzc[ scan8[i] ])
                ff_h264_idct_add_8_mmi(dest[j-1] + block_offset[i],
                        block + i*16, stride);
            else if(((int16_t*)block)[i*16])
                ff_h264_idct_dc_add_8_mmi(dest[j-1] + block_offset[i],
                        block + i*16, stride);
        }
    }
}

void ff_h264_idct_add8_422_8_mmi(uint8_t **dest, const int *block_offset,
        int16_t *block, int stride, const uint8_t nnzc[15*8])
{
    int i, j;

    for(j=1; j<3; j++){
        for(i=j*16; i<j*16+4; i++){
            if(nnzc[ scan8[i] ])
                ff_h264_idct_add_8_mmi(dest[j-1] + block_offset[i],
                        block + i*16, stride);
            else if(((int16_t*)block)[i*16])
                ff_h264_idct_dc_add_8_mmi(dest[j-1] + block_offset[i],
                        block + i*16, stride);
        }
    }

    for(j=1; j<3; j++){
        for(i=j*16+4; i<j*16+8; i++){
            if(nnzc[ scan8[i+4] ])
                ff_h264_idct_add_8_mmi(dest[j-1] + block_offset[i+4],
                        block + i*16, stride);
            else if(((int16_t*)block)[i*16])
                ff_h264_idct_dc_add_8_mmi(dest[j-1] + block_offset[i+4],
                        block + i*16, stride);
        }
    }
}

void ff_h264_luma_dc_dequant_idct_8_mmi(int16_t *output, int16_t *input,
        int qmul)
{
    double ftmp[10];
    uint64_t tmp[2];

    __asm__ volatile (
        ".set       noreorder                                           \n\t"
        "dli        %[tmp0],    0x08                                    \n\t"
        "ldc1       %[ftmp3],   0x18(%[input])                          \n\t"
        "mtc1       %[tmp0],    %[ftmp8]                                \n\t"
        "ldc1       %[ftmp2],   0x10(%[input])                          \n\t"
        "dli        %[tmp0],    0x20                                    \n\t"
        "ldc1       %[ftmp1],   0x08(%[input])                          \n\t"
        "mtc1       %[tmp0],    %[ftmp9]                                \n\t"
        "ldc1       %[ftmp0],   0x00(%[input])                          \n\t"
        "mov.d      %[ftmp4],   %[ftmp3]                                \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp2]                \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "mov.d      %[ftmp4],   %[ftmp1]                                \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "psubh      %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "mov.d      %[ftmp4],   %[ftmp3]                                \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "psubh      %[ftmp1],   %[ftmp1],       %[ftmp4]                \n\t"
        "mov.d      %[ftmp4],   %[ftmp2]                                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "psubh      %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "mov.d      %[ftmp4],   %[ftmp3]                                \n\t"
        "punpcklhw  %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "punpckhhw  %[ftmp4],   %[ftmp4],       %[ftmp1]                \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhwd  %[ftmp2],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklwd  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "mov.d      %[ftmp0],   %[ftmp4]                                \n\t"
        "punpcklwd  %[ftmp4],   %[ftmp4],       %[ftmp1]                \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "mov.d      %[ftmp1],   %[ftmp0]                                \n\t"
        "paddh      %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "psubh      %[ftmp4],   %[ftmp4],       %[ftmp1]                \n\t"
        "mov.d      %[ftmp1],   %[ftmp2]                                \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "mov.d      %[ftmp1],   %[ftmp0]                                \n\t"
        "paddh      %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp1]                \n\t"
        "mov.d      %[ftmp1],   %[ftmp4]                                \n\t"
        "daddi      %[tmp0],    %[qmul],        -0x7fff                 \n\t"
        "paddh      %[ftmp4],   %[ftmp4],       %[ftmp3]                \n\t"
        "bgtz       %[tmp0],    1f                                      \n\t"
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "ori        %[tmp0],    $0,             0x80                    \n\t"
        "dsll       %[tmp0],    %[tmp0],        0x10                    \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp0],       %[ff_pw_1]              \n\t"
        "daddu      %[qmul],    %[qmul],        %[tmp0]                 \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ff_pw_1]              \n\t"
        "punpckhhw  %[ftmp5],   %[ftmp2],       %[ff_pw_1]              \n\t"
        "punpcklhw  %[ftmp2],   %[ftmp2],       %[ff_pw_1]              \n\t"
        "mtc1       %[qmul],    %[ftmp7]                                \n\t"
        "punpcklwd  %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp0],   %[ftmp0],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "psraw      %[ftmp0],   %[ftmp0],       %[ftmp8]                \n\t"
        "psraw      %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "psraw      %[ftmp1],   %[ftmp1],       %[ftmp8]                \n\t"
        "psraw      %[ftmp5],   %[ftmp5],       %[ftmp8]                \n\t"
        "packsswh   %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "packsswh   %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "dmfc1      %[tmp1],    %[ftmp0]                                \n\t"
        "dsrl       %[ftmp0],   %[ftmp0],       %[ftmp9]                \n\t"
        "mfc1       %[input],   %[ftmp0]                                \n\t"
        "sh         %[tmp1],    0x00(%[output])                         \n\t"
        "sh         %[input],   0x80(%[output])                         \n\t"
        "dsrl       %[tmp1],    %[tmp1],        0x10                    \n\t"
        PTR_SRL    "%[input],   %[input],       0x10                    \n\t"
        "sh         %[tmp1],    0x20(%[output])                         \n\t"
        "sh         %[input],   0xa0(%[output])                         \n\t"
        "dmfc1      %[tmp1],    %[ftmp2]                                \n\t"
        "dsrl       %[ftmp2],   %[ftmp2],       %[ftmp9]                \n\t"
        "mfc1       %[input],   %[ftmp2]                                \n\t"
        "sh         %[tmp1],    0x40(%[output])                         \n\t"
        "sh         %[input],   0xc0(%[output])                         \n\t"
        "dsrl       %[tmp1],    %[tmp1],        0x10                    \n\t"
        PTR_SRL    "%[input],   %[input],       0x10                    \n\t"
        "sh         %[tmp1],    0x60(%[output])                         \n\t"
        "sh         %[input],   0xe0(%[output])                         \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp3],       %[ff_pw_1]              \n\t"
        "punpcklhw  %[ftmp3],   %[ftmp3],       %[ff_pw_1]              \n\t"
        "punpckhhw  %[ftmp5],   %[ftmp4],       %[ff_pw_1]              \n\t"
        "punpcklhw  %[ftmp4],   %[ftmp4],       %[ff_pw_1]              \n\t"
        "mtc1       %[qmul],    %[ftmp7]                                \n\t"
        "punpcklwd  %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp4],   %[ftmp4],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "psraw      %[ftmp3],   %[ftmp3],       %[ftmp8]                \n\t"
        "psraw      %[ftmp4],   %[ftmp4],       %[ftmp8]                \n\t"
        "psraw      %[ftmp1],   %[ftmp1],       %[ftmp8]                \n\t"
        "psraw      %[ftmp5],   %[ftmp5],       %[ftmp8]                \n\t"
        "packsswh   %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "packsswh   %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "dmfc1      %[tmp1],    %[ftmp3]                                \n\t"
        "dsrl       %[ftmp3],   %[ftmp3],       %[ftmp9]                \n\t"
        "mfc1       %[input],   %[ftmp3]                                \n\t"
        "sh         %[tmp1],    0x100(%[output])                        \n\t"
        "sh         %[input],   0x180(%[output])                        \n\t"
        "dsrl       %[tmp1],    %[tmp1],        0x10                    \n\t"
        PTR_SRL    "%[input],   %[input],       0x10                    \n\t"
        "sh         %[tmp1],    0x120(%[output])                        \n\t"
        "sh         %[input],   0x1a0(%[output])                        \n\t"
        "dmfc1      %[tmp1],    %[ftmp4]                                \n\t"
        "dsrl       %[ftmp4],   %[ftmp4],       %[ftmp9]                \n\t"
        "mfc1       %[input],   %[ftmp4]                                \n\t"
        "sh         %[tmp1],    0x140(%[output])                        \n\t"
        "sh         %[input],   0x1c0(%[output])                        \n\t"
        "dsrl       %[tmp1],    %[tmp1],        0x10                    \n\t"
        PTR_SRL    "%[input],   %[input],       0x10                    \n\t"
        "sh         %[tmp1],    0x160(%[output])                        \n\t"
        "j          2f                                                  \n\t"
        "sh         %[input],   0x1e0(%[output])                        \n\t"
        "1:                                                             \n\t"
        "ori        %[tmp0],    $0,             0x1f                    \n\t"
        "clz        %[tmp1],    %[qmul]                                 \n\t"
        "ori        %[input],   $0,             0x07                    \n\t"
        "dsubu      %[tmp1],    %[tmp0],        %[tmp1]                 \n\t"
        "ori        %[tmp0],    $0,             0x80                    \n\t"
        "dsll       %[tmp0],    %[tmp0],        0x10                    \n\t"
        "daddu      %[qmul],    %[qmul],        %[tmp0]                 \n\t"
        "dsubu      %[tmp0],    %[tmp1],        %[input]                \n\t"
        "movn       %[tmp1],    %[input],       %[tmp0]                 \n\t"
        PTR_ADDIU  "%[input],   %[input],       0x01                    \n\t"
        "andi       %[tmp0],    %[tmp1],        0xff                    \n\t"
        "srlv       %[qmul],    %[qmul],        %[tmp0]                 \n\t"
        PTR_SUBU   "%[input],   %[input],       %[tmp1]                 \n\t"
        "mtc1       %[input],   %[ftmp6]                                \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp0],       %[ff_pw_1]              \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ff_pw_1]              \n\t"
        "punpckhhw  %[ftmp5],   %[ftmp2],       %[ff_pw_1]              \n\t"
        "punpcklhw  %[ftmp2],   %[ftmp2],       %[ff_pw_1]              \n\t"
        "mtc1       %[qmul],    %[ftmp7]                                \n\t"
        "punpcklwd  %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp0],   %[ftmp0],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "psraw      %[ftmp0],   %[ftmp0],       %[ftmp6]                \n\t"
        "psraw      %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "psraw      %[ftmp1],   %[ftmp1],       %[ftmp6]                \n\t"
        "psraw      %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "packsswh   %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "packsswh   %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "dmfc1      %[tmp1],    %[ftmp0]                                \n\t"
        "dsrl       %[ftmp0],   %[ftmp0],       %[ftmp9]                \n\t"
        "sh         %[tmp1],    0x00(%[output])                         \n\t"
        "mfc1       %[input],   %[ftmp0]                                \n\t"
        "dsrl       %[tmp1],    %[tmp1],        0x10                    \n\t"
        "sh         %[input],   0x80(%[output])                         \n\t"
        "sh         %[tmp1],    0x20(%[output])                         \n\t"
        PTR_SRL    "%[input],   %[input],       0x10                    \n\t"
        "dmfc1      %[tmp1],    %[ftmp2]                                \n\t"
        "sh         %[input],   0xa0(%[output])                         \n\t"
        "dsrl       %[ftmp2],   %[ftmp2],       %[ftmp9]                \n\t"
        "sh         %[tmp1],    0x40(%[output])                         \n\t"
        "mfc1       %[input],   %[ftmp2]                                \n\t"
        "dsrl       %[tmp1],    %[tmp1],        0x10                    \n\t"
        "sh         %[input],   0xc0(%[output])                         \n\t"
        "sh         %[tmp1],    0x60(%[output])                         \n\t"
        PTR_SRL    "%[input],   %[input],       0x10                    \n\t"
        "sh         %[input],   0xe0(%[output])                         \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp3],       %[ff_pw_1]              \n\t"
        "punpcklhw  %[ftmp3],   %[ftmp3],       %[ff_pw_1]              \n\t"
        "punpckhhw  %[ftmp5],   %[ftmp4],       %[ff_pw_1]              \n\t"
        "punpcklhw  %[ftmp4],   %[ftmp4],       %[ff_pw_1]              \n\t"
        "mtc1       %[qmul],    %[ftmp7]                                \n\t"
        "punpcklwd  %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp4],   %[ftmp4],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "pmaddhw    %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "psraw      %[ftmp3],   %[ftmp3],       %[ftmp6]                \n\t"
        "psraw      %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "psraw      %[ftmp1],   %[ftmp1],       %[ftmp6]                \n\t"
        "psraw      %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "packsswh   %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "packsswh   %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "dmfc1      %[tmp1],    %[ftmp3]                                \n\t"
        "dsrl       %[ftmp3],   %[ftmp3],       %[ftmp9]                \n\t"
        "mfc1       %[input],   %[ftmp3]                                \n\t"
        "sh         %[tmp1],    0x100(%[output])                        \n\t"
        "sh         %[input],   0x180(%[output])                        \n\t"
        "dsrl       %[tmp1],    %[tmp1],        0x10                    \n\t"
        PTR_SRL    "%[input],   %[input],       0x10                    \n\t"
        "sh         %[tmp1],    0x120(%[output])                        \n\t"
        "sh         %[input],   0x1a0(%[output])                        \n\t"
        "dmfc1      %[tmp1],    %[ftmp4]                                \n\t"
        "dsrl       %[ftmp4],   %[ftmp4],       %[ftmp9]                \n\t"
        "mfc1       %[input],   %[ftmp4]                                \n\t"
        "sh         %[tmp1],    0x140(%[output])                        \n\t"
        "sh         %[input],   0x1c0(%[output])                        \n\t"
        "dsrl       %[tmp1],    %[tmp1],        0x10                    \n\t"
        PTR_SRL    "%[input],   %[input],       0x10                    \n\t"
        "sh         %[tmp1],    0x160(%[output])                        \n\t"
        "sh         %[input],   0x1e0(%[output])                        \n\t"
        "2:                                                             \n\t"
        ".set       reorder                                             \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          [output]"+&r"(output),            [input]"+&r"(input),
          [qmul]"+&r"(qmul)
        : [ff_pw_1]"f"(ff_pw_1)
        : "memory"
    );
}

void ff_h264_chroma422_dc_dequant_idct_8_mmi(int16_t *block, int qmul)
{
    int temp[8];
    int t[8];

    temp[0] = block[0] + block[16];
    temp[1] = block[0] - block[16];
    temp[2] = block[32] + block[48];
    temp[3] = block[32] - block[48];
    temp[4] = block[64] + block[80];
    temp[5] = block[64] - block[80];
    temp[6] = block[96] + block[112];
    temp[7] = block[96] - block[112];

    t[0] = temp[0] + temp[4] + temp[2] + temp[6];
    t[1] = temp[0] - temp[4] + temp[2] - temp[6];
    t[2] = temp[0] - temp[4] - temp[2] + temp[6];
    t[3] = temp[0] + temp[4] - temp[2] - temp[6];
    t[4] = temp[1] + temp[5] + temp[3] + temp[7];
    t[5] = temp[1] - temp[5] + temp[3] - temp[7];
    t[6] = temp[1] - temp[5] - temp[3] + temp[7];
    t[7] = temp[1] + temp[5] - temp[3] - temp[7];

    block[  0]= (t[0]*qmul + 128) >> 8;
    block[ 32]= (t[1]*qmul + 128) >> 8;
    block[ 64]= (t[2]*qmul + 128) >> 8;
    block[ 96]= (t[3]*qmul + 128) >> 8;
    block[ 16]= (t[4]*qmul + 128) >> 8;
    block[ 48]= (t[5]*qmul + 128) >> 8;
    block[ 80]= (t[6]*qmul + 128) >> 8;
    block[112]= (t[7]*qmul + 128) >> 8;
}

void ff_h264_chroma_dc_dequant_idct_8_mmi(int16_t *block, int qmul)
{
    int a,b,c,d;

    d = block[0] - block[16];
    a = block[0] + block[16];
    b = block[32] - block[48];
    c = block[32] + block[48];
    block[0] = ((a+c)*qmul) >> 7;
    block[16]= ((d+b)*qmul) >> 7;
    block[32]= ((a-c)*qmul) >> 7;
    block[48]= ((d-b)*qmul) >> 7;
}

void ff_h264_weight_pixels16_8_mmi(uint8_t *block, ptrdiff_t stride, int height,
        int log2_denom, int weight, int offset)
{
    int y;
    double ftmp[8];

    offset <<= log2_denom;

    if (log2_denom)
        offset += 1 << (log2_denom - 1);

    for (y=0; y<height; y++, block+=stride) {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "ldc1       %[ftmp1],   0x00(%[block0])                     \n\t"
            "ldc1       %[ftmp2],   0x00(%[block1])                     \n\t"
            "mtc1       %[weight],  %[ftmp3]                            \n\t"
            "mtc1       %[offset],  %[ftmp4]                            \n\t"
            "mtc1       %[log2_denom],              %[ftmp5]            \n\t"
            "pshufh     %[ftmp3],   %[ftmp3],       %[ftmp0]            \n\t"
            "pshufh     %[ftmp4],   %[ftmp4],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp6],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp7],   %[ftmp2],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[ftmp3]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "pmullh     %[ftmp2],   %[ftmp2],       %[ftmp3]            \n\t"
            "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"
            "paddsh     %[ftmp7],   %[ftmp7],       %[ftmp4]            \n\t"
            "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp4]            \n\t"
            "paddsh     %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "psrah      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t"
            "psrah      %[ftmp7],   %[ftmp7],       %[ftmp5]            \n\t"
            "psrah      %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
            "psrah      %[ftmp2],   %[ftmp2],       %[ftmp5]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp6]            \n\t"
            "packushb   %[ftmp2],   %[ftmp2],       %[ftmp7]            \n\t"
            "sdc1       %[ftmp1],   0x00(%[block0])                     \n\t"
            "sdc1       %[ftmp2],   0x00(%[block1])                     \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7])
            : [block0]"r"(block),           [block1]"r"(block+8),
              [weight]"r"(weight),          [offset]"r"(offset),
              [log2_denom]"r"(log2_denom)
            : "memory"
        );
    }
}

void ff_h264_biweight_pixels16_8_mmi(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
        int height, int log2_denom, int weightd, int weights, int offset)
{
    int y;
    double ftmp[9];

    offset = ((offset + 1) | 1) << log2_denom;

    for (y=0; y<height; y++, dst+=stride, src+=stride) {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "ldc1       %[ftmp1],   0x00(%[src0])                       \n\t"
            "ldc1       %[ftmp2],   0x00(%[dst0])                       \n\t"
            "mtc1       %[weights], %[ftmp3]                            \n\t"
            "mtc1       %[weightd], %[ftmp4]                            \n\t"
            "mtc1       %[offset],  %[ftmp5]                            \n\t"
            "mtc1       %[log2_denom],              %[ftmp6]            \n\t"
            "pshufh     %[ftmp3],   %[ftmp3],       %[ftmp0]            \n\t"
            "pshufh     %[ftmp4],   %[ftmp4],       %[ftmp0]            \n\t"
            "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp7],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp2],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[ftmp3]            \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[ftmp4]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "pmullh     %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "paddsh     %[ftmp7],   %[ftmp7],       %[ftmp5]            \n\t"
            "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
            "paddsh     %[ftmp7],   %[ftmp7],       %[ftmp8]            \n\t"
            "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "psrah      %[ftmp7],   %[ftmp7],       %[ftmp6]            \n\t"
            "psrah      %[ftmp1],   %[ftmp1],       %[ftmp6]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "sdc1       %[ftmp1],   0x00(%[dst0])                       \n\t"
            "ldc1       %[ftmp1],   0x00(%[src1])                       \n\t"
            "ldc1       %[ftmp2],   0x00(%[dst1])                       \n\t"
            "punpckhbh  %[ftmp7],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp2],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[ftmp3]            \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[ftmp4]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "pmullh     %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "paddsh     %[ftmp7],   %[ftmp7],       %[ftmp5]            \n\t"
            "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
            "paddsh     %[ftmp7],   %[ftmp7],       %[ftmp8]            \n\t"
            "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "psrah      %[ftmp7],   %[ftmp7],       %[ftmp6]            \n\t"
            "psrah      %[ftmp1],   %[ftmp1],       %[ftmp6]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "sdc1       %[ftmp1],   0x00(%[dst1])                       \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8])
            : [dst0]"r"(dst),               [dst1]"r"(dst+8),
              [src0]"r"(src),               [src1]"r"(src+8),
              [weights]"r"(weights),        [weightd]"r"(weightd),
              [offset]"r"(offset),          [log2_denom]"r"(log2_denom+1)
            : "memory"
        );
    }
}

void ff_h264_weight_pixels8_8_mmi(uint8_t *block, ptrdiff_t stride, int height,
        int log2_denom, int weight, int offset)
{
    int y;
    double ftmp[6];

    offset <<= log2_denom;

    if (log2_denom)
        offset += 1 << (log2_denom - 1);

    for (y=0; y<height; y++, block+=stride) {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "ldc1       %[ftmp1],   0x00(%[block])                      \n\t"
            "mtc1       %[weight],  %[ftmp2]                            \n\t"
            "mtc1       %[offset],  %[ftmp3]                            \n\t"
            "mtc1       %[log2_denom],              %[ftmp5]            \n\t"
            "pshufh     %[ftmp2],   %[ftmp2],       %[ftmp0]            \n\t"
            "pshufh     %[ftmp3],   %[ftmp3],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp4],   %[ftmp4],       %[ftmp2]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "paddsh     %[ftmp4],   %[ftmp4],       %[ftmp3]            \n\t"
            "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "psrah      %[ftmp4],   %[ftmp4],       %[ftmp5]            \n\t"
            "psrah      %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp4]            \n\t"
            "sdc1       %[ftmp1],   0x00(%[block])                      \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5])
            : [block]"r"(block),            [weight]"r"(weight),
              [offset]"r"(offset),          [log2_denom]"r"(log2_denom)
            : "memory"
        );
    }
}

void ff_h264_biweight_pixels8_8_mmi(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
        int height, int log2_denom, int weightd, int weights, int offset)
{
    int y;
    double ftmp[9];

    offset = ((offset + 1) | 1) << log2_denom;

    for (y=0; y<height; y++, dst+=stride, src+=stride) {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "ldc1       %[ftmp1],   0x00(%[src])                        \n\t"
            "ldc1       %[ftmp2],   0x00(%[dst])                        \n\t"
            "mtc1       %[weights], %[ftmp3]                            \n\t"
            "mtc1       %[weightd], %[ftmp4]                            \n\t"
            "mtc1       %[offset],  %[ftmp5]                            \n\t"
            "mtc1       %[log2_denom],              %[ftmp6]            \n\t"
            "pshufh     %[ftmp3],   %[ftmp3],       %[ftmp0]            \n\t"
            "pshufh     %[ftmp4],   %[ftmp4],       %[ftmp0]            \n\t"
            "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp7],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpckhbh  %[ftmp8],   %[ftmp2],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp7],   %[ftmp7],       %[ftmp3]            \n\t"
            "pmullh     %[ftmp8],   %[ftmp8],       %[ftmp4]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "pmullh     %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "paddsh     %[ftmp7],   %[ftmp7],       %[ftmp5]            \n\t"
            "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
            "paddsh     %[ftmp7],   %[ftmp7],       %[ftmp8]            \n\t"
            "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "psrah      %[ftmp7],   %[ftmp7],       %[ftmp6]            \n\t"
            "psrah      %[ftmp1],   %[ftmp1],       %[ftmp6]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp7]            \n\t"
            "sdc1       %[ftmp1],   0x00(%[dst])                        \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
              [ftmp8]"=&f"(ftmp[8])
            : [dst]"r"(dst),                [src]"r"(src),
              [weights]"r"(weights),        [weightd]"r"(weightd),
              [offset]"r"(offset),          [log2_denom]"r"(log2_denom+1)
            : "memory"
        );
    }
}

void ff_h264_weight_pixels4_8_mmi(uint8_t *block, ptrdiff_t stride, int height,
        int log2_denom, int weight, int offset)
{
    int y;
    double ftmp[5];
    uint64_t low32;

    offset <<= log2_denom;

    if (log2_denom)
        offset += 1 << (log2_denom - 1);

    for (y=0; y<height; y++, block+=stride) {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "uld        %[low32],   0x00(%[block])                      \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "mtc1       %[weight],  %[ftmp2]                            \n\t"
            "mtc1       %[offset],  %[ftmp3]                            \n\t"
            "mtc1       %[log2_denom],              %[ftmp4]            \n\t"
            "pshufh     %[ftmp2],   %[ftmp2],       %[ftmp0]            \n\t"
            "pshufh     %[ftmp3],   %[ftmp3],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "psrah      %[ftmp1],   %[ftmp1],       %[ftmp4]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "gsswlc1    %[ftmp1],   0x03(%[block])                      \n\t"
            "gsswrc1    %[ftmp1],   0x00(%[block])                      \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),
              [low32]"=&r"(low32)
            : [block]"r"(block),            [weight]"r"(weight),
              [offset]"r"(offset),          [log2_denom]"r"(log2_denom)
            : "memory"
        );
    }
}

void ff_h264_biweight_pixels4_8_mmi(uint8_t *dst, uint8_t *src, ptrdiff_t stride,
        int height, int log2_denom, int weightd, int weights, int offset)
{
    int y;
    double ftmp[7];
    uint64_t low32;

    offset = ((offset + 1) | 1) << log2_denom;

    for (y=0; y<height; y++, dst+=stride, src+=stride) {
        __asm__ volatile (
            "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
            "uld        %[low32],   0x00(%[src])                        \n\t"
            "mtc1       %[low32],   %[ftmp1]                            \n\t"
            "uld        %[low32],   0x00(%[dst])                        \n\t"
            "mtc1       %[low32],   %[ftmp2]                            \n\t"
            "mtc1       %[weight],  %[ftmp3]                            \n\t"
            "mtc1       %[weightd], %[ftmp4]                            \n\t"
            "mtc1       %[offset],  %[ftmp5]                            \n\t"
            "mtc1       %[log2_denom],              %[ftmp6]            \n\t"
            "pshufh     %[ftmp3],   %[ftmp3],       %[ftmp0]            \n\t"
            "pshufh     %[ftmp4],   %[ftmp4],       %[ftmp0]            \n\t"
            "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]            \n\t"
            "pmullh     %[ftmp1],   %[ftmp1],       %[ftmp3]            \n\t"
            "pmullh     %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t"
            "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
            "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp2]            \n\t"
            "psrah      %[ftmp1],   %[ftmp1],       %[ftmp6]            \n\t"
            "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
            "gsswlc1    %[ftmp1],   0x03(%[dst])                        \n\t"
            "gsswrc1    %[ftmp1],   0x00(%[dst])                        \n\t"
            : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
              [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
              [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
              [ftmp6]"=&f"(ftmp[6]),
              [low32]"=&r"(low32)
            : [dst]"r"(dst),                [src]"r"(src),
              [weight]"r"(weights),         [weightd]"r"(weightd),
              [offset]"r"(offset),          [log2_denom]"r"(log2_denom+1)
            : "memory"
        );
    }
}

void ff_deblock_v8_luma_8_mmi(uint8_t *pix, int stride, int alpha, int beta,
        int8_t *tc0)
{
    double ftmp[12];
    mips_reg addr[2];
    uint64_t low32;

    __asm__ volatile (
        PTR_ADDU   "%[addr0],   %[stride],      %[stride]               \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        PTR_ADDU   "%[addr1],   %[stride],      %[addr0]                \n\t"
        "addi       %[alpha],   %[alpha],       -0x01                   \n\t"
        PTR_SUBU   "%[addr1],   $0,             %[addr1]                \n\t"
        "addi       %[beta],    %[beta],        -0x01                   \n\t"
        PTR_ADDU   "%[addr1],   %[addr1],       %[pix]                  \n\t"
        "ldc1       %[ftmp3],   0x00(%[pix])                            \n\t"
        "gsldxc1    %[ftmp1],   0x00(%[addr1],  %[stride])              \n\t"
        "gsldxc1    %[ftmp2],   0x00(%[addr1],  %[addr0])               \n\t"
        "gsldxc1    %[ftmp4],   0x00(%[pix],    %[stride])              \n\t"
        "mtc1       %[alpha],   %[ftmp5]                                \n\t"
        "mtc1       %[beta],    %[ftmp6]                                \n\t"
        "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "pshufh     %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "packushb   %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp3],       %[ftmp2]                \n\t"
        "psubusb    %[ftmp8],   %[ftmp2],       %[ftmp3]                \n\t"
        "or         %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp2],       %[ftmp1]                \n\t"
        "psubusb    %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp1],       %[ftmp2]                \n\t"
        "or         %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "or         %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp4],       %[ftmp3]                \n\t"
        "or         %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "or         %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
        "pcmpeqb    %[ftmp8],   %[ftmp8],       %[ftmp0]                \n\t"
        "pcmpeqb    %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        "uld        %[low32],   0x00(%[tc0])                            \n\t"
        "mtc1       %[low32],   %[ftmp5]                                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp5],       %[ftmp5]                \n\t"
        "pcmpgtb    %[ftmp5],   %[ftmp9],       %[ftmp4]                \n\t"
        "ldc1       %[ftmp4],   0x00(%[addr1])                          \n\t"
        "and        %[ftmp10],  %[ftmp5],       %[ftmp8]                \n\t"
        "psubusb    %[ftmp8],   %[ftmp4],       %[ftmp2]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp2],       %[ftmp4]                \n\t"
        "psubusb    %[ftmp8],   %[ftmp8],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp7],       %[ftmp6]                \n\t"
        "pcmpeqb    %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
        "and        %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
        "and        %[ftmp5],   %[ftmp10],      %[ftmp9]                \n\t"
        "psubb      %[ftmp8],   %[ftmp5],       %[ftmp7]                \n\t"
        "and        %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"
        "pavgb      %[ftmp5],   %[ftmp2],       %[ftmp3]                \n\t"
        "ldc1       %[ftmp11],  0x00(%[addr1])                          \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "xor        %[ftmp5],   %[ftmp5],       %[ftmp11]               \n\t"
        "and        %[ftmp5],   %[ftmp5],       %[ff_pb_1]              \n\t"
        "psubusb    %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp1],       %[ftmp7]                \n\t"
        "paddusb    %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "pmaxub     %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "pminub     %[ftmp4],   %[ftmp4],       %[ftmp7]                \n\t"
        "gssdxc1    %[ftmp4],   0x00(%[addr1],  %[stride])              \n\t"
        "gsldxc1    %[ftmp5],   0x00(%[pix],    %[addr0])               \n\t"
        "psubusb    %[ftmp4],   %[ftmp5],       %[ftmp3]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp3],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp7],       %[ftmp6]                \n\t"
        "pcmpeqb    %[ftmp7],   %[ftmp7],       %[ftmp4]                \n\t"
        "and        %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
        "psubb      %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
        "and        %[ftmp6],   %[ftmp9],       %[ftmp7]                \n\t"
        "gsldxc1    %[ftmp4],   0x00(%[pix],    %[stride])              \n\t"
        "pavgb      %[ftmp7],   %[ftmp2],       %[ftmp3]                \n\t"
        "gsldxc1    %[ftmp11],  0x00(%[pix],    %[addr0])               \n\t"
        "pavgb      %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "xor        %[ftmp7],   %[ftmp7],       %[ftmp11]               \n\t"
        "and        %[ftmp7],   %[ftmp7],       %[ff_pb_1]              \n\t"
        "psubusb    %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp4],       %[ftmp6]                \n\t"
        "paddusb    %[ftmp6],   %[ftmp6],       %[ftmp4]                \n\t"
        "pmaxub     %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "pminub     %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "gssdxc1    %[ftmp5],   0x00(%[pix],    %[stride])              \n\t"
        "xor        %[ftmp6],   %[ftmp2],       %[ftmp3]                \n\t"
        "pcmpeqb    %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        "and        %[ftmp6],   %[ftmp6],       %[ff_pb_1]              \n\t"
        "xor        %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "xor        %[ftmp5],   %[ftmp5],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ftmp1]                \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ff_pb_3]              \n\t"
        "pavgb      %[ftmp5],   %[ftmp5],       %[ftmp3]                \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "paddusb    %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp7],   %[ff_pb_A1],    %[ftmp4]                \n\t"
        "psubusb    %[ftmp4],   %[ftmp4],       %[ff_pb_A1]             \n\t"
        "pminub     %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
        "pminub     %[ftmp4],   %[ftmp4],       %[ftmp8]                \n\t"
        "psubusb    %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "paddusb    %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "paddusb    %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "gssdxc1    %[ftmp2],   0x00(%[addr1],  %[addr0])               \n\t"
        "sdc1       %[ftmp3],   0x00(%[pix])                            \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [low32]"=&r"(low32)
        : [pix]"r"(pix),                    [stride]"r"((mips_reg)stride),
          [alpha]"r"((mips_reg)alpha),      [beta]"r"((mips_reg)beta),
          [tc0]"r"(tc0),                    [ff_pb_1]"f"(ff_pb_1),
          [ff_pb_3]"f"(ff_pb_3),            [ff_pb_A1]"f"(ff_pb_A1)
        : "memory"
    );
}

static void deblock_v8_luma_intra_8_mmi(uint8_t *pix, int stride, int alpha,
        int beta)
{
    DECLARE_ALIGNED(8, const uint64_t, stack[0x0a]);
    double ftmp[16];
    uint64_t tmp[1];
    mips_reg addr[3];

__asm__ volatile (
"ori        %[tmp0],    $0,             0x01                    \n\t"
"xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
"mtc1       %[tmp0],    %[ftmp9]                                \n\t"
PTR_SLL    "%[addr0],   %[stride],      0x02                    \n\t"
PTR_ADDU   "%[addr2],   %[stride],      %[stride]               \n\t"
PTR_ADDIU  "%[alpha],   %[alpha],       -0x01                   \n\t"
PTR_SLL    "%[ftmp11],  %[ftmp9],       %[ftmp9]                \n\t"
"bltz       %[alpha],   1f                                      \n\t"
PTR_ADDU   "%[addr1],   %[addr2],       %[stride]               \n\t"
PTR_ADDIU  "%[beta],    %[beta],        -0x01                   \n\t"
"bltz       %[beta],    1f                                      \n\t"
PTR_SUBU   "%[addr0],   $0,             %[addr0]                \n\t"
PTR_ADDU   "%[addr0],   %[addr0],       %[pix]                  \n\t"
"ldc1       %[ftmp3],   0x00(%[pix])                            \n\t"
"gsldxc1    %[ftmp1],   0x00(%[addr0],  %[addr2])               \n\t"
"gsldxc1    %[ftmp2],   0x00(%[addr0],  %[addr1])               \n\t"
"gsldxc1    %[ftmp4],   0x00(%[pix],    %[stride])              \n\t"
"mtc1       %[alpha],   %[ftmp5]                                \n\t"
"mtc1       %[beta],    %[ftmp6]                                \n\t"
"pshufh     %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
"pshufh     %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
"packushb   %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
"psubusb    %[ftmp7],   %[ftmp3],       %[ftmp2]                \n\t"
"psubusb    %[ftmp8],   %[ftmp2],       %[ftmp3]                \n\t"
"packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
"or         %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
"sdc1       %[ftmp5],   0x10+%[stack]                           \n\t"
"psubusb    %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
"psubusb    %[ftmp7],   %[ftmp2],       %[ftmp1]                \n\t"
"psubusb    %[ftmp5],   %[ftmp1],       %[ftmp2]                \n\t"
"or         %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
"psubusb    %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
"or         %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
"psubusb    %[ftmp7],   %[ftmp3],       %[ftmp4]                \n\t"
"psubusb    %[ftmp5],   %[ftmp4],       %[ftmp3]                \n\t"
"or         %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
"psubusb    %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
"or         %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
"xor        %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
"ldc1       %[ftmp5],   0x10+%[stack]                           \n\t"
"pcmpeqb    %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
"ldc1       %[ftmp10],  %[ff_pb_1]                              \n\t"
"sdc1       %[ftmp8],   0x20+%[stack]                           \n\t"
"pavgb      %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
"psubusb    %[ftmp8],   %[ftmp3],       %[ftmp2]                \n\t"
"pavgb      %[ftmp5],   %[ftmp5],       %[ftmp10]               \n\t"
"psubusb    %[ftmp7],   %[ftmp2],       %[ftmp3]                \n\t"
"psubusb    %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
"psubusb    %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"
"ldc1       %[ftmp15],  0x20+%[stack]                           \n\t"
"pcmpeqb    %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
"and        %[ftmp7],   %[ftmp7],       %[ftmp15]               \n\t"
"gsldxc1    %[ftmp15],  0x00(%[addr0],  %[stride])              \n\t"
"psubusb    %[ftmp8],   %[ftmp15],      %[ftmp2]                \n\t"
"psubusb    %[ftmp5],   %[ftmp2],       %[ftmp15]               \n\t"
"psubusb    %[ftmp8],   %[ftmp8],       %[ftmp6]                \n\t"
"psubusb    %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
"pcmpeqb    %[ftmp5],   %[ftmp5],       %[ftmp8]                \n\t"
"and        %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
"gsldxc1    %[ftmp14],  0x00(%[pix],    %[addr2])               \n\t"
"sdc1       %[ftmp5],   0x30+%[stack]                           \n\t"
"psubusb    %[ftmp8],   %[ftmp14],      %[ftmp3]                \n\t"
"psubusb    %[ftmp5],   %[ftmp3],       %[ftmp14]               \n\t"
"psubusb    %[ftmp8],   %[ftmp8],       %[ftmp6]                \n\t"
"psubusb    %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
"pcmpeqb    %[ftmp5],   %[ftmp5],       %[ftmp8]                \n\t"
"and        %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
"sdc1       %[ftmp5],   0x40+%[stack]                           \n\t"
"pavgb      %[ftmp5],   %[ftmp15],      %[ftmp1]                \n\t"
"pavgb      %[ftmp6],   %[ftmp2],       %[ftmp3]                \n\t"
"pavgb      %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
"sdc1       %[ftmp6],   0x10+%[stack]                           \n\t"
"paddb      %[ftmp7],   %[ftmp15],      %[ftmp1]                \n\t"
"paddb      %[ftmp8],   %[ftmp2],       %[ftmp3]                \n\t"
"paddb      %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
"mov.d      %[ftmp8],   %[ftmp7]                                \n\t"
"sdc1       %[ftmp7],   0x00+%[stack]                           \n\t"
"psrlh      %[ftmp7],   %[ftmp7],       %[ftmp9]                \n\t"
"pavgb      %[ftmp7],   %[ftmp7],       %[ftmp0]                \n\t"
"xor        %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"
"and        %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
"psubb      %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
"pavgb      %[ftmp6],   %[ftmp15],      %[ftmp4]                \n\t"
"psubb      %[ftmp7],   %[ftmp15],      %[ftmp4]                \n\t"
"paddb      %[ftmp8],   %[ftmp8],       %[ftmp8]                \n\t"
"psubb      %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
"and        %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
"psubb      %[ftmp6],   %[ftmp6],       %[ftmp7]                \n\t"
"ldc1       %[ftmp13],  0x10+%[stack]                           \n\t"
"pavgb      %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
"psrlh      %[ftmp8],   %[ftmp8],       %[ftmp11]               \n\t"
"pavgb      %[ftmp6],   %[ftmp6],       %[ftmp13]               \n\t"
"pavgb      %[ftmp8],   %[ftmp8],       %[ftmp0]                \n\t"
"xor        %[ftmp8],   %[ftmp8],       %[ftmp6]                \n\t"
"and        %[ftmp8],   %[ftmp8],       %[ftmp10]               \n\t"
"psubb      %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
"xor        %[ftmp8],   %[ftmp2],       %[ftmp4]                \n\t"
"pavgb      %[ftmp7],   %[ftmp2],       %[ftmp4]                \n\t"
"and        %[ftmp8],   %[ftmp8],       %[ftmp10]               \n\t"
"psubb      %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
"ldc1       %[ftmp13],  0x30+%[stack]                           \n\t"
"pavgb      %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
"ldc1       %[ftmp12],  0x20+%[stack]                           \n\t"
"xor        %[ftmp6],   %[ftmp6],       %[ftmp7]                \n\t"
"xor        %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
"and        %[ftmp6],   %[ftmp6],       %[ftmp13]               \n\t"
"and        %[ftmp7],   %[ftmp7],       %[ftmp12]               \n\t"
"xor        %[ftmp6],   %[ftmp6],       %[ftmp7]                \n\t"
"xor        %[ftmp6],   %[ftmp6],       %[ftmp2]                \n\t"
"gssdxc1    %[ftmp6],   0x00(%[addr0],  %[addr1])               \n\t"
"ldc1       %[ftmp6],   0x00(%[addr0])                          \n\t"
"paddb      %[ftmp7],   %[ftmp15],      %[ftmp6]                \n\t"
"pavgb      %[ftmp6],   %[ftmp6],       %[ftmp15]               \n\t"
"ldc1       %[ftmp12],  0x00+%[stack]                           \n\t"
"pavgb      %[ftmp6],   %[ftmp6],       %[ftmp5]                \n\t"
"paddb      %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
"paddb      %[ftmp7],   %[ftmp7],       %[ftmp12]               \n\t"
"psrlh      %[ftmp7],   %[ftmp7],       %[ftmp11]               \n\t"
"pavgb      %[ftmp7],   %[ftmp7],       %[ftmp0]                \n\t"
"xor        %[ftmp7],   %[ftmp7],       %[ftmp6]                \n\t"
"and        %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
"ldc1       %[ftmp12],  0x30+%[stack]                           \n\t"
"psubb      %[ftmp6],   %[ftmp6],       %[ftmp7]                \n\t"
"xor        %[ftmp5],   %[ftmp5],       %[ftmp1]                \n\t"
"xor        %[ftmp6],   %[ftmp6],       %[ftmp15]               \n\t"
"and        %[ftmp5],   %[ftmp5],       %[ftmp12]               \n\t"
"and        %[ftmp6],   %[ftmp6],       %[ftmp12]               \n\t"
"xor        %[ftmp5],   %[ftmp5],       %[ftmp1]                \n\t"
"xor        %[ftmp6],   %[ftmp6],       %[ftmp15]               \n\t"
"gssdxc1    %[ftmp5],   0x00(%[addr0],  %[addr2])               \n\t"
"gssdxc1    %[ftmp6],   0x00(%[addr0],  %[stride])              \n\t"
"pavgb      %[ftmp5],   %[ftmp14],      %[ftmp4]                \n\t"
"pavgb      %[ftmp6],   %[ftmp3],       %[ftmp2]                \n\t"
"pavgb      %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
"sdc1       %[ftmp6],   0x10+%[stack]                           \n\t"
"paddb      %[ftmp7],   %[ftmp14],      %[ftmp4]                \n\t"
"paddb      %[ftmp8],   %[ftmp3],       %[ftmp2]                \n\t"
"paddb      %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
"mov.d      %[ftmp8],   %[ftmp7]                                \n\t"
"sdc1       %[ftmp7],   0x00+%[stack]                           \n\t"
"psrlh      %[ftmp7],   %[ftmp7],       %[ftmp9]                \n\t"
"pavgb      %[ftmp7],   %[ftmp7],       %[ftmp0]                \n\t"
"xor        %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"
"and        %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
"psubb      %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
"pavgb      %[ftmp6],   %[ftmp14],      %[ftmp1]                \n\t"
"paddb      %[ftmp8],   %[ftmp8],       %[ftmp8]                \n\t"
"psubb      %[ftmp7],   %[ftmp14],      %[ftmp1]                \n\t"
"psubb      %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
"and        %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
"psubb      %[ftmp6],   %[ftmp6],       %[ftmp7]                \n\t"
"ldc1       %[ftmp12],  0x10+%[stack]                           \n\t"
"pavgb      %[ftmp6],   %[ftmp6],       %[ftmp4]                \n\t"
"pavgb      %[ftmp6],   %[ftmp6],       %[ftmp12]               \n\t"
"psrlh      %[ftmp8],   %[ftmp8],       %[ftmp11]               \n\t"
"pavgb      %[ftmp8],   %[ftmp8],       %[ftmp0]                \n\t"
"xor        %[ftmp8],   %[ftmp8],       %[ftmp6]                \n\t"
"and        %[ftmp8],   %[ftmp8],       %[ftmp10]               \n\t"
"psubb      %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
"xor        %[ftmp8],   %[ftmp3],       %[ftmp1]                \n\t"
"pavgb      %[ftmp7],   %[ftmp3],       %[ftmp1]                \n\t"
"and        %[ftmp8],   %[ftmp8],       %[ftmp10]               \n\t"
"ldc1       %[ftmp12],  0x40+%[stack]                           \n\t"
"psubb      %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
"ldc1       %[ftmp13],  0x20+%[stack]                           \n\t"
"pavgb      %[ftmp7],   %[ftmp7],       %[ftmp4]                \n\t"
"xor        %[ftmp6],   %[ftmp6],       %[ftmp7]                \n\t"
"xor        %[ftmp7],   %[ftmp7],       %[ftmp3]                \n\t"
"and        %[ftmp6],   %[ftmp6],       %[ftmp12]               \n\t"
"and        %[ftmp7],   %[ftmp7],       %[ftmp13]               \n\t"
"xor        %[ftmp6],   %[ftmp6],       %[ftmp7]                \n\t"
"xor        %[ftmp6],   %[ftmp6],       %[ftmp3]                \n\t"
"sdc1       %[ftmp6],   0x00(%[pix])                            \n\t"
"gsldxc1    %[ftmp6],   0x00(%[pix],    %[addr1])               \n\t"
"paddb      %[ftmp7],   %[ftmp14],      %[ftmp6]                \n\t"
"pavgb      %[ftmp6],   %[ftmp6],       %[ftmp14]               \n\t"
"ldc1       %[ftmp12],  0x00+%[stack]                           \n\t"
"pavgb      %[ftmp6],   %[ftmp6],       %[ftmp5]                \n\t"
"paddb      %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
"paddb      %[ftmp7],   %[ftmp7],       %[ftmp12]               \n\t"
"psrlh      %[ftmp7],   %[ftmp7],       %[ftmp11]               \n\t"
"pavgb      %[ftmp7],   %[ftmp7],       %[ftmp0]                \n\t"
"xor        %[ftmp7],   %[ftmp7],       %[ftmp6]                \n\t"
"and        %[ftmp7],   %[ftmp7],       %[ftmp10]               \n\t"
"ldc1       %[ftmp12],  0x40+%[stack]                           \n\t"
"psubb      %[ftmp6],   %[ftmp6],       %[ftmp7]                \n\t"
"xor        %[ftmp5],   %[ftmp5],       %[ftmp4]                \n\t"
"xor        %[ftmp6],   %[ftmp6],       %[ftmp14]               \n\t"
"and        %[ftmp5],   %[ftmp5],       %[ftmp12]               \n\t"
"and        %[ftmp6],   %[ftmp6],       %[ftmp12]               \n\t"
"xor        %[ftmp5],   %[ftmp5],       %[ftmp4]                \n\t"
"xor        %[ftmp6],   %[ftmp6],       %[ftmp14]               \n\t"
"gssdxc1    %[ftmp5],   0x00(%[pix],    %[stride])              \n\t"
"gssdxc1    %[ftmp6],   0x00(%[pix],    %[addr2])               \n\t"
"1:                                                             \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
          [ftmp12]"=&f"(ftmp[12]),          [ftmp13]"=&f"(ftmp[13]),
          [ftmp14]"=&f"(ftmp[14]),          [ftmp15]"=&f"(ftmp[15]),
  [tmp0]"=&r"(tmp[0]),
  [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
  [addr2]"=&r"(addr[2]),
  [alpha]"+&r"(alpha),              [beta]"+&r"(beta)
        : [pix]"r"(pix),                    [stride]"r"((mips_reg)stride),
  [stack]"m"(stack[0]),             [ff_pb_1]"m"(ff_pb_1)
: "memory"
);
}

void ff_deblock_v_chroma_8_mmi(uint8_t *pix, int stride, int alpha, int beta,
        int8_t *tc0)
{
    double ftmp[9];
    mips_reg addr[1];
    uint64_t low32;

    __asm__ volatile (
        "addi       %[alpha],   %[alpha],       -0x01                   \n\t"
        "addi       %[beta],    %[beta],        -0x01                   \n\t"
        "or         %[addr0],   $0,             %[pix]                  \n\t"
        PTR_SUBU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        PTR_SUBU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "ldc1       %[ftmp1],   0x00(%[addr0])                          \n\t"
        "gsldxc1    %[ftmp2],   0x00(%[addr0],  %[stride])              \n\t"
        "ldc1       %[ftmp3],   0x00(%[pix])                            \n\t"
        "gsldxc1    %[ftmp4],   0x00(%[pix],    %[stride])              \n\t"

        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "mtc1       %[alpha],   %[ftmp5]                                \n\t"
        "mtc1       %[beta],    %[ftmp6]                                \n\t"
        "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "pshufh     %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "packushb   %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp3],       %[ftmp2]                \n\t"
        "psubusb    %[ftmp8],   %[ftmp2],       %[ftmp3]                \n\t"
        "or         %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp2],       %[ftmp1]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp1],       %[ftmp2]                \n\t"
        "or         %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "or         %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp4],       %[ftmp3]                \n\t"
        "or         %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "or         %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
        "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "pcmpeqb    %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
        "uld        %[low32],   0x00(%[tc0])                            \n\t"
        "mtc1       %[low32],   %[ftmp7]                                \n\t"
        "punpcklbh  %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "and        %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
        "pcmpeqb    %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        "xor        %[ftmp6],   %[ftmp2],       %[ftmp3]                \n\t"
        "xor        %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "and        %[ftmp6],   %[ftmp6],       %[ff_pb_1]              \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ftmp1]                \n\t"
        "xor        %[ftmp5],   %[ftmp5],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ff_pb_3]              \n\t"
        "pavgb      %[ftmp5],   %[ftmp5],       %[ftmp3]                \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "paddusb    %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp7],   %[ff_pb_A1],    %[ftmp4]                \n\t"
        "psubusb    %[ftmp4],   %[ftmp4],       %[ff_pb_A1]             \n\t"
        "pminub     %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
        "pminub     %[ftmp4],   %[ftmp4],       %[ftmp8]                \n\t"
        "psubusb    %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "paddusb    %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "paddusb    %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"

        "gssdxc1    %[ftmp2],   0x00(%[addr0],  %[stride])              \n\t"
        "sdc1       %[ftmp3],   0x00(%[pix])                            \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [addr0]"=&r"(addr[0]),
          [low32]"=&r"(low32)
        : [pix]"r"(pix),                    [stride]"r"((mips_reg)stride),
          [alpha]"r"(alpha),                [beta]"r"(beta),
          [tc0]"r"(tc0),                    [ff_pb_1]"f"(ff_pb_1),
          [ff_pb_3]"f"(ff_pb_3),            [ff_pb_A1]"f"(ff_pb_A1)
        : "memory"
    );
}

void ff_deblock_v_chroma_intra_8_mmi(uint8_t *pix, int stride, int alpha,
        int beta)
{
    double ftmp[9];
    mips_reg addr[1];

    __asm__ volatile (
        "addi       %[alpha],   %[alpha],       -0x01                   \n\t"
        "addi       %[beta],    %[beta],        -0x01                   \n\t"
        "or         %[addr0],   $0,             %[pix]                  \n\t"
        PTR_SUBU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        PTR_SUBU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "ldc1       %[ftmp1],   0x00(%[addr0])                          \n\t"
        "gsldxc1    %[ftmp2],   0x00(%[addr0],  %[stride])              \n\t"
        "ldc1       %[ftmp3],   0x00(%[pix])                            \n\t"
        "gsldxc1    %[ftmp4],   0x00(%[pix],    %[stride])              \n\t"

        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "mtc1       %[alpha],   %[ftmp5]                                \n\t"
        "mtc1       %[beta],    %[ftmp6]                                \n\t"
        "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "pshufh     %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "packushb   %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        "packushb   %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp3],       %[ftmp2]                \n\t"
        "psubusb    %[ftmp8],   %[ftmp2],       %[ftmp3]                \n\t"
        "or         %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp2],       %[ftmp1]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp1],       %[ftmp2]                \n\t"
        "or         %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "or         %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp4],       %[ftmp3]                \n\t"
        "or         %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp5],   %[ftmp5],       %[ftmp6]                \n\t"
        "or         %[ftmp8],   %[ftmp8],       %[ftmp5]                \n\t"
        "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]                \n\t"
        "pcmpeqb    %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
        "mov.d      %[ftmp6],   %[ftmp2]                                \n\t"
        "mov.d      %[ftmp7],   %[ftmp3]                                \n\t"
        "xor        %[ftmp5],   %[ftmp2],       %[ftmp4]                \n\t"
        "and        %[ftmp5],   %[ftmp5],       %[ff_pb_1]              \n\t"
        "pavgb      %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "psubusb    %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "pavgb      %[ftmp2],   %[ftmp2],       %[ftmp1]                \n\t"
        "xor        %[ftmp5],   %[ftmp3],       %[ftmp1]                \n\t"
        "and        %[ftmp5],   %[ftmp5],       %[ff_pb_1]              \n\t"
        "pavgb      %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "psubusb    %[ftmp3],   %[ftmp3],       %[ftmp5]                \n\t"
        "pavgb      %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubb      %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "psubb      %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "and        %[ftmp2],   %[ftmp2],       %[ftmp8]                \n\t"
        "and        %[ftmp3],   %[ftmp3],       %[ftmp8]                \n\t"
        "paddb      %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "paddb      %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"

        "gssdxc1    %[ftmp2],   0x00(%[addr0],  %[stride])              \n\t"
        "sdc1       %[ftmp3],   0x00(%[pix])                            \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [addr0]"=&r"(addr[0])
        : [pix]"r"(pix),                    [stride]"r"((mips_reg)stride),
          [alpha]"r"(alpha),                [beta]"r"(beta),
          [ff_pb_1]"f"(ff_pb_1)
        : "memory"
    );
}

void ff_deblock_h_chroma_8_mmi(uint8_t *pix, int stride, int alpha, int beta,
        int8_t *tc0)
{
    double ftmp[11];
    mips_reg addr[6];
    uint64_t low32;

    __asm__ volatile (
        "addi       %[alpha],   %[alpha],       -0x01                   \n\t"
        "addi       %[beta],    %[beta],        -0x01                   \n\t"
        PTR_ADDU   "%[addr0],   %[stride],      %[stride]               \n\t"
        PTR_ADDI   "%[pix],     %[pix],         -0x02                   \n\t"
        PTR_ADDU   "%[addr1],   %[addr0],       %[stride]               \n\t"
        PTR_ADDU   "%[addr2],   %[addr0],       %[addr0]                \n\t"
        "or         %[addr5],   $0,             %[pix]                  \n\t"
        PTR_ADDU   "%[pix],     %[pix],         %[addr1]                \n\t"
        "uld        %[low32],   0x00(%[addr5])                          \n\t"
        "mtc1       %[low32],   %[ftmp0]                                \n\t"
        PTR_ADDU   "%[addr3],   %[addr5],       %[stride]               \n\t"
        "uld        %[low32],   0x00(%[addr3])                          \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        PTR_ADDU   "%[addr4],   %[addr5],       %[addr0]                \n\t"
        "uld        %[low32],   0x00(%[addr4])                          \n\t"
        "mtc1       %[low32],   %[ftmp1]                                \n\t"
        "uld        %[low32],   0x00(%[pix])                            \n\t"
        "mtc1       %[low32],   %[ftmp3]                                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        PTR_ADDU   "%[addr3],   %[pix],         %[stride]               \n\t"
        "punpckhhw  %[ftmp2],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "uld        %[low32],   0x00(%[addr3])                          \n\t"
        "mtc1       %[low32],   %[ftmp4]                                \n\t"
        PTR_ADDU   "%[addr4],   %[pix],         %[addr0]                \n\t"
        "uld        %[low32],   0x00(%[addr4])                          \n\t"
        "mtc1       %[low32],   %[ftmp6]                                \n\t"
        PTR_ADDU   "%[addr3],   %[pix],         %[addr1]                \n\t"
        "uld        %[low32],   0x00(%[addr3])                          \n\t"
        "mtc1       %[low32],   %[ftmp5]                                \n\t"
        PTR_ADDU   "%[addr4],   %[pix],         %[addr2]                \n\t"
        "uld        %[low32],   0x00(%[addr4])                          \n\t"
        "mtc1       %[low32],   %[ftmp7]                                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "mov.d      %[ftmp6],   %[ftmp4]                                \n\t"
        "punpcklhw  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpckhhw  %[ftmp6],   %[ftmp6],       %[ftmp5]                \n\t"
        "punpckhwd  %[ftmp1],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpckhwd  %[ftmp3],   %[ftmp2],       %[ftmp6]                \n\t"
        "punpcklwd  %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpcklwd  %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "mov.d      %[ftmp9],   %[ftmp0]                                \n\t"
        "mov.d      %[ftmp10],  %[ftmp3]                                \n\t"

        "xor        %[ftmp8],   %[ftmp8],       %[ftmp8]                \n\t"
        "mtc1       %[alpha],   %[ftmp4]                                \n\t"
        "mtc1       %[beta],    %[ftmp5]                                \n\t"
        "pshufh     %[ftmp4],   %[ftmp4],       %[ftmp8]                \n\t"
        "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp8]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        "packushb   %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp6],   %[ftmp2],       %[ftmp1]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp1],       %[ftmp2]                \n\t"
        "or         %[ftmp7],   %[ftmp7],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp7],       %[ftmp4]                \n\t"
        "psubusb    %[ftmp6],   %[ftmp1],       %[ftmp0]                \n\t"
        "psubusb    %[ftmp4],   %[ftmp0],       %[ftmp1]                \n\t"
        "or         %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "or         %[ftmp7],   %[ftmp7],       %[ftmp4]                \n\t"
        "psubusb    %[ftmp6],   %[ftmp2],       %[ftmp3]                \n\t"
        "psubusb    %[ftmp4],   %[ftmp3],       %[ftmp2]                \n\t"
        "or         %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "or         %[ftmp7],   %[ftmp7],       %[ftmp4]                \n\t"
        "xor        %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "pcmpeqb    %[ftmp7],   %[ftmp7],       %[ftmp6]                \n\t"
        "uld        %[low32],   0x00(%[tc0])                            \n\t"
        "mtc1       %[low32],   %[ftmp6]                                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "and        %[ftmp7],   %[ftmp7],       %[ftmp6]                \n\t"
        "pcmpeqb    %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        "xor        %[ftmp5],   %[ftmp1],       %[ftmp2]                \n\t"
        "xor        %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "and        %[ftmp5],   %[ftmp5],       %[ff_pb_1]              \n\t"
        "pavgb      %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "xor        %[ftmp4],   %[ftmp4],       %[ftmp1]                \n\t"
        "pavgb      %[ftmp3],   %[ftmp3],       %[ff_pb_3]              \n\t"
        "pavgb      %[ftmp4],   %[ftmp4],       %[ftmp2]                \n\t"
        "pavgb      %[ftmp3],   %[ftmp3],       %[ftmp5]                \n\t"
        "paddusb    %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "psubusb    %[ftmp6],   %[ff_pb_A1],    %[ftmp3]                \n\t"
        "psubusb    %[ftmp3],   %[ftmp3],       %[ff_pb_A1]             \n\t"
        "pminub     %[ftmp6],   %[ftmp6],       %[ftmp7]                \n\t"
        "pminub     %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "psubusb    %[ftmp1],   %[ftmp1],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "paddusb    %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "paddusb    %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"

        "punpckhwd  %[ftmp4],   %[ftmp9],       %[ftmp9]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp1],       %[ftmp1]                \n\t"
        "punpckhwd  %[ftmp6],   %[ftmp2],       %[ftmp2]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp9],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp10]               \n\t"
        "punpcklhw  %[ftmp1],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "gsswlc1    %[ftmp1],   0x03(%[addr5])                          \n\t"
        "gsswrc1    %[ftmp1],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr5],       %[stride]               \n\t"
        "punpckhwd  %[ftmp1],   %[ftmp1],       %[ftmp1]                \n\t"
        "gsswlc1    %[ftmp1],   0x03(%[addr3])                          \n\t"
        PTR_ADDU   "%[addr4],   %[addr5],       %[addr0]                \n\t"
        "gsswrc1    %[ftmp1],   0x00(%[addr3])                          \n\t"
        "gsswlc1    %[ftmp0],   0x03(%[addr4])                          \n\t"
        "gsswrc1    %[ftmp0],   0x00(%[addr4])                          \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpckhwd  %[ftmp3],   %[ftmp10],      %[ftmp10]               \n\t"
        "gsswlc1    %[ftmp0],   0x03(%[pix])                            \n\t"
        "gsswrc1    %[ftmp0],   0x00(%[pix])                            \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp3]                \n\t"
        PTR_ADDU   "%[addr3],   %[pix],         %[stride]               \n\t"
        "punpcklhw  %[ftmp5],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpckhhw  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "gsswlc1    %[ftmp5],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp5],   0x00(%[addr3])                          \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        PTR_ADDU   "%[addr3],   %[pix],         %[addr0]                \n\t"
        PTR_ADDU   "%[addr4],   %[pix],         %[addr1]                \n\t"
        "gsswlc1    %[ftmp5],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp5],   0x00(%[addr3])                          \n\t"
        "gsswlc1    %[ftmp4],   0x03(%[addr4])                          \n\t"
        PTR_ADDU   "%[addr3],   %[pix],         %[addr2]                \n\t"
        "punpckhwd  %[ftmp9],   %[ftmp4],       %[ftmp4]                \n\t"
        "gsswrc1    %[ftmp4],   0x00(%[addr4])                          \n\t"
        "gsswlc1    %[ftmp9],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp9],   0x00(%[addr3])                          \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),            [addr5]"=&r"(addr[5]),
          [pix]"+&r"(pix),
          [low32]"=&r"(low32)
        : [alpha]"r"(alpha),                [beta]"r"(beta),
          [stride]"r"((mips_reg)stride),    [tc0]"r"(tc0),
          [ff_pb_1]"f"(ff_pb_1),            [ff_pb_3]"f"(ff_pb_3),
          [ff_pb_A1]"f"(ff_pb_A1)
        : "memory"
    );
}

void ff_deblock_h_chroma_intra_8_mmi(uint8_t *pix, int stride, int alpha,
        int beta)
{
    double ftmp[11];
    mips_reg addr[6];
    uint64_t low32;

    __asm__ volatile (
        "addi       %[alpha],   %[alpha],       -0x01                   \n\t"
        "addi       %[beta],    %[beta],        -0x01                   \n\t"
        PTR_ADDU   "%[addr0],   %[stride],      %[stride]               \n\t"
        PTR_ADDI   "%[pix],     %[pix],         -0x02                   \n\t"
        PTR_ADDU   "%[addr1],   %[addr0],       %[stride]               \n\t"
        PTR_ADDU   "%[addr2],   %[addr0],       %[addr0]                \n\t"
        "or         %[addr5],   $0,             %[pix]                  \n\t"
        PTR_ADDU   "%[pix],     %[pix],         %[addr1]                \n\t"
        "uld        %[low32],   0x00(%[addr5])                          \n\t"
        "mtc1       %[low32],   %[ftmp0]                                \n\t"
        PTR_ADDU   "%[addr3],   %[addr5],       %[stride]               \n\t"
        "uld        %[low32],   0x00(%[addr3])                          \n\t"
        "mtc1       %[low32],   %[ftmp2]                                \n\t"
        PTR_ADDU   "%[addr4],   %[addr5],       %[addr0]                \n\t"
        "uld        %[low32],   0x00(%[addr4])                          \n\t"
        "mtc1       %[low32],   %[ftmp1]                                \n\t"
        "uld        %[low32],   0x00(%[pix])                            \n\t"
        "mtc1       %[low32],   %[ftmp3]                                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        PTR_ADDU   "%[addr3],   %[pix],         %[stride]               \n\t"
        "punpckhhw  %[ftmp2],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "uld        %[low32],   0x00(%[addr3])                          \n\t"
        "mtc1       %[low32],   %[ftmp4]                                \n\t"
        PTR_ADDU   "%[addr4],   %[pix],         %[addr0]                \n\t"
        "uld        %[low32],   0x00(%[addr4])                          \n\t"
        "mtc1       %[low32],   %[ftmp6]                                \n\t"
        PTR_ADDU   "%[addr3],   %[pix],         %[addr1]                \n\t"
        "uld        %[low32],   0x00(%[addr3])                          \n\t"
        "mtc1       %[low32],   %[ftmp5]                                \n\t"
        PTR_ADDU   "%[addr4],   %[pix],         %[addr2]                \n\t"
        "uld        %[low32],   0x00(%[addr4])                          \n\t"
        "mtc1       %[low32],   %[ftmp7]                                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp7]                \n\t"
        "mov.d      %[ftmp6],   %[ftmp4]                                \n\t"
        "punpcklhw  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpckhhw  %[ftmp6],   %[ftmp6],       %[ftmp5]                \n\t"
        "punpckhwd  %[ftmp1],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpckhwd  %[ftmp3],   %[ftmp2],       %[ftmp6]                \n\t"
        "punpcklwd  %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpcklwd  %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"

        "xor        %[ftmp8],   %[ftmp8],       %[ftmp8]                \n\t"
        "mtc1       %[alpha],   %[ftmp4]                                \n\t"
        "mtc1       %[beta],    %[ftmp5]                                \n\t"
        "pshufh     %[ftmp4],   %[ftmp4],       %[ftmp8]                \n\t"
        "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp8]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        "packushb   %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        "psubusb    %[ftmp6],   %[ftmp2],       %[ftmp1]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp1],       %[ftmp2]                \n\t"
        "or         %[ftmp7],   %[ftmp7],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp7],   %[ftmp7],       %[ftmp4]                \n\t"
        "psubusb    %[ftmp6],   %[ftmp1],       %[ftmp0]                \n\t"
        "psubusb    %[ftmp4],   %[ftmp0],       %[ftmp1]                \n\t"
        "or         %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "or         %[ftmp7],   %[ftmp7],       %[ftmp4]                \n\t"
        "psubusb    %[ftmp6],   %[ftmp2],       %[ftmp3]                \n\t"
        "psubusb    %[ftmp4],   %[ftmp3],       %[ftmp2]                \n\t"
        "or         %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "psubusb    %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "or         %[ftmp7],   %[ftmp7],       %[ftmp4]                \n\t"
        "xor        %[ftmp6],   %[ftmp6],       %[ftmp6]                \n\t"
        "pcmpeqb    %[ftmp7],   %[ftmp7],       %[ftmp6]                \n\t"
        "mov.d      %[ftmp5],   %[ftmp1]                                \n\t"
        "mov.d      %[ftmp6],   %[ftmp2]                                \n\t"
        "xor        %[ftmp4],   %[ftmp1],       %[ftmp3]                \n\t"
        "and        %[ftmp4],   %[ftmp4],       %[ff_pb_1]              \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "psubusb    %[ftmp1],   %[ftmp1],       %[ftmp4]                \n\t"
        "pavgb      %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "xor        %[ftmp4],   %[ftmp2],       %[ftmp0]                \n\t"
        "and        %[ftmp4],   %[ftmp4],       %[ff_pb_1]              \n\t"
        "pavgb      %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "psubusb    %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "pavgb      %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "psubb      %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        "psubb      %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "and        %[ftmp1],   %[ftmp1],       %[ftmp7]                \n\t"
        "and        %[ftmp2],   %[ftmp2],       %[ftmp7]                \n\t"
        "paddb      %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        "paddb      %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"

        "punpckhwd  %[ftmp4],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp1],       %[ftmp1]                \n\t"
        "punpckhwd  %[ftmp6],   %[ftmp2],       %[ftmp2]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpcklhw  %[ftmp1],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "gsswlc1    %[ftmp1],   0x03(%[addr5])                          \n\t"
        "gsswrc1    %[ftmp1],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr5],       %[stride]               \n\t"
        "punpckhwd  %[ftmp1],   %[ftmp1],       %[ftmp1]                \n\t"
        "gsswlc1    %[ftmp1],   0x03(%[addr3])                          \n\t"
        PTR_ADDU   "%[addr4],   %[addr5],       %[addr0]                \n\t"
        "gsswrc1    %[ftmp1],   0x00(%[addr3])                          \n\t"
        "gsswlc1    %[ftmp0],   0x03(%[addr4])                          \n\t"
        "gsswrc1    %[ftmp0],   0x00(%[addr4])                          \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpckhwd  %[ftmp3],   %[ftmp3],       %[ftmp3]                \n\t"
        "gsswlc1    %[ftmp0],   0x03(%[pix])                            \n\t"
        "gsswrc1    %[ftmp0],   0x00(%[pix])                            \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp3]                \n\t"
        PTR_ADDU   "%[addr3],   %[pix],         %[stride]               \n\t"
        "punpcklhw  %[ftmp5],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpckhhw  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "gsswlc1    %[ftmp5],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp5],   0x00(%[addr3])                          \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        PTR_ADDU   "%[addr3],   %[pix],         %[addr0]                \n\t"
        PTR_ADDU   "%[addr4],   %[pix],         %[addr1]                \n\t"
        "gsswlc1    %[ftmp5],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp5],   0x00(%[addr3])                          \n\t"
        "gsswlc1    %[ftmp4],   0x03(%[addr4])                          \n\t"
        PTR_ADDU   "%[addr3],   %[pix],         %[addr2]                \n\t"
        "punpckhwd  %[ftmp9],   %[ftmp4],       %[ftmp4]                \n\t"
        "gsswrc1    %[ftmp4],   0x00(%[addr4])                          \n\t"
        "gsswlc1    %[ftmp9],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp9],   0x00(%[addr3])                          \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),            [addr5]"=&r"(addr[5]),
          [pix]"+&r"(pix),
          [low32]"=&r"(low32)
        : [alpha]"r"(alpha),                [beta]"r"(beta),
          [stride]"r"((mips_reg)stride),    [ff_pb_1]"f"(ff_pb_1)
        : "memory"
    );
}

void ff_deblock_v_luma_8_mmi(uint8_t *pix, int stride, int alpha, int beta,
        int8_t *tc0)
{
    if ((tc0[0] & tc0[1]) >= 0)
        ff_deblock_v8_luma_8_mmi(pix + 0, stride, alpha, beta, tc0);
    if ((tc0[2] & tc0[3]) >= 0)
        ff_deblock_v8_luma_8_mmi(pix + 8, stride, alpha, beta, tc0 + 2);
}

void ff_deblock_v_luma_intra_8_mmi(uint8_t *pix, int stride, int alpha,
        int beta)
{
    deblock_v8_luma_intra_8_mmi(pix + 0, stride, alpha, beta);
    deblock_v8_luma_intra_8_mmi(pix + 8, stride, alpha, beta);
}

void ff_deblock_h_luma_8_mmi(uint8_t *pix, int stride, int alpha, int beta,
        int8_t *tc0)
{
    uint64_t stack[0xd];
    double ftmp[9];
    mips_reg addr[8];

    __asm__ volatile (
        PTR_ADDU   "%[addr0],   %[stride],      %[stride]               \n\t"
        PTR_ADDI   "%[addr1],   %[pix],         -0x4                    \n\t"
        PTR_ADDU   "%[addr2],   %[stride],      %[addr0]                \n\t"
        "gsldlc1    %[ftmp0],   0x07(%[addr1])                          \n\t"
        "gsldrc1    %[ftmp0],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr1],       %[stride]               \n\t"
        PTR_ADDU   "%[addr4],   %[addr1],       %[addr2]                \n\t"
        "gsldlc1    %[ftmp1],   0x07(%[addr3])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[addr0]                \n\t"
        "gsldrc1    %[ftmp1],   0x00(%[addr3])                          \n\t"
        "gsldlc1    %[ftmp2],   0x07(%[addr5])                          \n\t"
        "gsldrc1    %[ftmp2],   0x00(%[addr5])                          \n\t"
        "gsldlc1    %[ftmp3],   0x07(%[addr4])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[stride]               \n\t"
        "gsldrc1    %[ftmp3],   0x00(%[addr4])                          \n\t"
        "gsldlc1    %[ftmp4],   0x07(%[addr3])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr0]                \n\t"
        "gsldrc1    %[ftmp4],   0x00(%[addr3])                          \n\t"
        "gsldlc1    %[ftmp5],   0x07(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[addr2]                \n\t"
        "gsldrc1    %[ftmp5],   0x00(%[addr5])                          \n\t"
        "gsldlc1    %[ftmp6],   0x07(%[addr3])                          \n\t"
        "gsldrc1    %[ftmp6],   0x00(%[addr3])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr0],       %[addr0]                \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpckhbh  %[ftmp1],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[addr6]                \n\t"
        "sdc1       %[ftmp1],   0x10(%[stack])                          \n\t"
        "gsldlc1    %[ftmp8],   0x07(%[addr3])                          \n\t"
        "gsldrc1    %[ftmp8],   0x00(%[addr3])                          \n\t"
        PTR_ADDU   "%[addr7],   %[addr6],       %[addr6]                \n\t"
        "punpckhbh  %[ftmp5],   %[ftmp6],       %[ftmp8]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhhw  %[ftmp2],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpcklhw  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "ldc1       %[ftmp8],   0x10(%[stack])                          \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "sdc1       %[ftmp0],   0x00(%[stack])                          \n\t"
        "punpckhhw  %[ftmp6],   %[ftmp7],       %[ftmp8]                \n\t"
        "punpcklhw  %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
        "punpckhhw  %[ftmp0],   %[ftmp3],       %[ftmp5]                \n\t"
        "punpcklhw  %[ftmp3],   %[ftmp3],       %[ftmp5]                \n\t"
        "punpcklwd  %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp7],       %[ftmp3]                \n\t"
        "punpcklwd  %[ftmp7],   %[ftmp7],       %[ftmp3]                \n\t"
        "punpckhwd  %[ftmp3],   %[ftmp1],       %[ftmp2]                \n\t"
        "punpcklwd  %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "sdc1       %[ftmp1],   0x10(%[stack])                          \n\t"
        "sdc1       %[ftmp3],   0x20(%[stack])                          \n\t"
        "sdc1       %[ftmp7],   0x30(%[stack])                          \n\t"
        "sdc1       %[ftmp5],   0x40(%[stack])                          \n\t"
        "sdc1       %[ftmp6],   0x50(%[stack])                          \n\t"
        PTR_ADDU   "%[addr1],   %[addr1],       %[addr7]                \n\t"
        PTR_ADDU   "%[addr4],   %[addr4],       %[addr7]                \n\t"
        "gsldlc1    %[ftmp0],   0x07(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr1],       %[stride]               \n\t"
        "gsldrc1    %[ftmp0],   0x00(%[addr1])                          \n\t"
        "gsldlc1    %[ftmp1],   0x07(%[addr3])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[addr0]                \n\t"
        "gsldrc1    %[ftmp1],   0x00(%[addr3])                          \n\t"
        "gsldlc1    %[ftmp2],   0x07(%[addr5])                          \n\t"
        "gsldrc1    %[ftmp2],   0x00(%[addr5])                          \n\t"
        "gsldlc1    %[ftmp3],   0x07(%[addr4])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[stride]               \n\t"
        "gsldrc1    %[ftmp3],   0x00(%[addr4])                          \n\t"
        "gsldlc1    %[ftmp4],   0x07(%[addr3])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr0]                \n\t"
        "gsldrc1    %[ftmp4],   0x00(%[addr3])                          \n\t"
        "gsldlc1    %[ftmp5],   0x07(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[addr2]                \n\t"
        "gsldrc1    %[ftmp5],   0x00(%[addr5])                          \n\t"
        "gsldlc1    %[ftmp6],   0x07(%[addr3])                          \n\t"
        "gsldrc1    %[ftmp6],   0x00(%[addr3])                          \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpckhbh  %[ftmp1],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[addr6]                \n\t"
        "sdc1       %[ftmp1],   0x18(%[stack])                          \n\t"
        "gsldlc1    %[ftmp8],   0x07(%[addr3])                          \n\t"
        "gsldrc1    %[ftmp8],   0x00(%[addr3])                          \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhbh  %[ftmp5],   %[ftmp6],       %[ftmp8]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhhw  %[ftmp2],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpcklhw  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "ldc1       %[ftmp8],   0x18(%[stack])                          \n\t"
        "sdc1       %[ftmp0],   0x08(%[stack])                          \n\t"
        "punpckhhw  %[ftmp6],   %[ftmp7],       %[ftmp8]                \n\t"
        "punpcklhw  %[ftmp7],   %[ftmp7],       %[ftmp8]                \n\t"
        "punpckhhw  %[ftmp0],   %[ftmp3],       %[ftmp5]                \n\t"
        "punpcklhw  %[ftmp3],   %[ftmp3],       %[ftmp5]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp7],       %[ftmp3]                \n\t"
        "punpcklwd  %[ftmp7],   %[ftmp7],       %[ftmp3]                \n\t"
        "punpckhwd  %[ftmp3],   %[ftmp1],       %[ftmp2]                \n\t"
        "punpcklwd  %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "punpcklwd  %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "sdc1       %[ftmp1],   0x18(%[stack])                          \n\t"
        "sdc1       %[ftmp3],   0x28(%[stack])                          \n\t"
        "sdc1       %[ftmp7],   0x38(%[stack])                          \n\t"
        "sdc1       %[ftmp5],   0x48(%[stack])                          \n\t"
        "sdc1       %[ftmp6],   0x58(%[stack])                          \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),            [addr5]"=&r"(addr[5]),
          [addr6]"=&r"(addr[6]),            [addr7]"=&r"(addr[7])
        : [pix]"r"(pix),                    [stride]"r"((mips_reg)stride),
          [stack]"r"(stack)
        : "memory"
    );

    ff_deblock_v_luma_8_mmi((uint8_t *) &stack[6], 0x10, alpha, beta, tc0);

    __asm__ volatile (
        PTR_ADDU   "%[addr0],   %[stride],      %[stride]               \n\t"
        PTR_ADDI   "%[addr1],   %[pix],          -0x02                  \n\t"
        PTR_ADDU   "%[addr6],   %[addr0],       %[addr0]                \n\t"
        PTR_ADDU   "%[addr2],   %[addr0],       %[stride]               \n\t"
        PTR_ADDU   "%[addr7],   %[addr6],       %[addr6]                \n\t"
        PTR_ADDU   "%[addr4],   %[addr1],       %[addr2]                \n\t"
        "ldc1       %[ftmp0],   0x10(%[stack])                          \n\t"
        "ldc1       %[ftmp1],   0x20(%[stack])                          \n\t"
        "ldc1       %[ftmp2],   0x30(%[stack])                          \n\t"
        "ldc1       %[ftmp3],   0x40(%[stack])                          \n\t"
        "punpckhwd  %[ftmp4],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp1],       %[ftmp1]                \n\t"
        "punpckhwd  %[ftmp6],   %[ftmp2],       %[ftmp2]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpcklhw  %[ftmp1],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "gsswlc1    %[ftmp1],   0x03(%[addr1])                          \n\t"
        "gsswrc1    %[ftmp1],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr1],       %[stride]               \n\t"
        "punpckhwd  %[ftmp1],   %[ftmp1],       %[ftmp1]                \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[addr0]                \n\t"
        "gsswlc1    %[ftmp1],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp1],   0x00(%[addr3])                          \n\t"
        "gsswlc1    %[ftmp0],   0x03(%[addr5])                          \n\t"
        "gsswrc1    %[ftmp0],   0x00(%[addr5])                          \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpckhwd  %[ftmp3],   %[ftmp3],       %[ftmp3]                \n\t"
        "gsswlc1    %[ftmp0],   0x03(%[addr4])                          \n\t"
        "gsswrc1    %[ftmp0],   0x00(%[addr4])                          \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp3]                \n\t"
        "punpcklhw  %[ftmp5],   %[ftmp4],       %[ftmp6]                \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[stride]               \n\t"
        "punpckhhw  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "gsswlc1    %[ftmp5],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp5],   0x00(%[addr3])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[addr0]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr2]                \n\t"
        "gsswlc1    %[ftmp5],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp5],   0x00(%[addr3])                          \n\t"
        "gsswlc1    %[ftmp4],   0x03(%[addr5])                          \n\t"
        "gsswrc1    %[ftmp4],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[addr6]                \n\t"
        "punpckhwd  %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        PTR_ADDU   "%[addr1],   %[addr1],       %[addr7]                \n\t"
        "gsswlc1    %[ftmp4],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp4],   0x00(%[addr3])                          \n\t"
        PTR_ADDU   "%[addr4],   %[addr4],       %[addr7]                \n\t"
        "ldc1       %[ftmp0],   0x18(%[stack])                          \n\t"
        "ldc1       %[ftmp1],   0x28(%[stack])                          \n\t"
        "ldc1       %[ftmp2],   0x38(%[stack])                          \n\t"
        "ldc1       %[ftmp3],   0x48(%[stack])                          \n\t"
        PTR_ADDU   "%[addr0],   %[stride],      %[stride]               \n\t"
        "punpckhwd  %[ftmp4],   %[ftmp0],       %[ftmp0]                \n\t"
        PTR_ADDU   "%[addr6],   %[addr0],       %[addr0]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp1],       %[ftmp1]                \n\t"
        "punpckhwd  %[ftmp6],   %[ftmp2],       %[ftmp2]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        PTR_ADDU   "%[addr3],   %[addr1],       %[stride]               \n\t"
        "punpcklhw  %[ftmp1],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "gsswlc1    %[ftmp1],   0x03(%[addr1])                          \n\t"
        "gsswrc1    %[ftmp1],   0x00(%[addr1])                          \n\t"
        "punpckhwd  %[ftmp1],   %[ftmp1],       %[ftmp1]                \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[addr0]                \n\t"
        "gsswlc1    %[ftmp1],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp1],   0x00(%[addr3])                          \n\t"
        "gsswlc1    %[ftmp0],   0x03(%[addr5])                          \n\t"
        "gsswrc1    %[ftmp0],   0x00(%[addr5])                          \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "punpckhwd  %[ftmp3],   %[ftmp3],       %[ftmp3]                \n\t"
        "gsswlc1    %[ftmp0],   0x03(%[addr4])                          \n\t"
        "gsswrc1    %[ftmp0],   0x00(%[addr4])                          \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp3]                \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[stride]               \n\t"
        "punpcklhw  %[ftmp5],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpckhhw  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "gsswlc1    %[ftmp5],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp5],   0x00(%[addr3])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[addr0]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp5],       %[ftmp5]                \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr2]                \n\t"
        "gsswlc1    %[ftmp5],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp5],   0x00(%[addr3])                          \n\t"
        "gsswlc1    %[ftmp4],   0x03(%[addr5])                          \n\t"
        "gsswrc1    %[ftmp4],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr4],       %[addr6]                \n\t"
        "punpckhwd  %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        "gsswlc1    %[ftmp4],   0x03(%[addr3])                          \n\t"
        "gsswrc1    %[ftmp4],   0x00(%[addr3])                          \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),            [addr5]"=&r"(addr[5]),
          [addr6]"=&r"(addr[6]),            [addr7]"=&r"(addr[7])
        : [pix]"r"(pix),                    [stride]"r"((mips_reg)stride),
          [stack]"r"(stack)
        : "memory"
    );
}

void ff_deblock_h_luma_intra_8_mmi(uint8_t *pix, int stride, int alpha,
        int beta)
{
    uint64_t ptmp[0x11];
    uint64_t pdat[4];
    double ftmp[9];
    mips_reg addr[7];

    __asm__ volatile (
        PTR_ADDU   "%[addr0],   %[stride],      %[stride]               \n\t"
        PTR_ADDI   "%[addr1],   %[pix],         -0x04                   \n\t"
        PTR_ADDU   "%[addr2],   %[addr0],       %[stride]               \n\t"
        PTR_ADDU   "%[addr3],   %[addr0],       %[addr0]                \n\t"
        PTR_ADDU   "%[addr4],   %[addr1],       %[addr2]                \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[stride]               \n\t"
        "gsldlc1    %[ftmp0],   0x07(%[addr1])                          \n\t"
        "gsldrc1    %[ftmp0],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr1],       %[addr0]                \n\t"
        "gsldlc1    %[ftmp1],   0x07(%[addr5])                          \n\t"
        "gsldrc1    %[ftmp1],   0x00(%[addr5])                          \n\t"
        "gsldlc1    %[ftmp2],   0x07(%[addr6])                          \n\t"
        "gsldrc1    %[ftmp2],   0x00(%[addr6])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[stride]               \n\t"
        "gsldlc1    %[ftmp3],   0x07(%[addr4])                          \n\t"
        "gsldrc1    %[ftmp3],   0x00(%[addr4])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr4],       %[addr0]                \n\t"
        "gsldlc1    %[ftmp4],   0x07(%[addr5])                          \n\t"
        "gsldrc1    %[ftmp4],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr2]                \n\t"
        "gsldlc1    %[ftmp5],   0x07(%[addr6])                          \n\t"
        "gsldrc1    %[ftmp5],   0x00(%[addr6])                          \n\t"
        "gsldlc1    %[ftmp6],   0x07(%[addr5])                          \n\t"
        "gsldrc1    %[ftmp6],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr3]                \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpckhbh  %[ftmp1],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "gsldlc1    %[ftmp8],   0x07(%[addr5])                          \n\t"
        "gsldrc1    %[ftmp8],   0x00(%[addr5])                          \n\t"
        "punpckhbh  %[ftmp5],   %[ftmp6],       %[ftmp8]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "sdc1       %[ftmp3],   0x00(%[ptmp])                           \n\t"
        "punpckhhw  %[ftmp3],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhhw  %[ftmp2],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpcklhw  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpckhhw  %[ftmp6],   %[ftmp7],       %[ftmp1]                \n\t"
        "punpcklhw  %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "sdc1       %[ftmp2],   0x20(%[ptmp])                           \n\t"
        "ldc1       %[ftmp2],   0x00(%[ptmp])                           \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp2],       %[ftmp5]                \n\t"
        "punpcklhw  %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpcklwd  %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpckhwd  %[ftmp4],   %[ftmp7],       %[ftmp2]                \n\t"
        "punpcklwd  %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
        "sdc1       %[ftmp0],   0x00(%[ptmp])                           \n\t"
        "sdc1       %[ftmp5],   0x10(%[ptmp])                           \n\t"
        "sdc1       %[ftmp7],   0x40(%[ptmp])                           \n\t"
        "sdc1       %[ftmp4],   0x50(%[ptmp])                           \n\t"
        "ldc1       %[ftmp8],   0x20(%[ptmp])                           \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp3],       %[ftmp8]                \n\t"
        "punpcklwd  %[ftmp3],   %[ftmp3],       %[ftmp8]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp6],       %[ftmp1]                \n\t"
        "punpcklwd  %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        PTR_ADDU   "%[addr5],   %[addr3],       %[addr3]                \n\t"
        "sdc1       %[ftmp3],   0x20(%[ptmp])                           \n\t"
        "sdc1       %[ftmp0],   0x30(%[ptmp])                           \n\t"
        "sdc1       %[ftmp6],   0x60(%[ptmp])                           \n\t"
        "sdc1       %[ftmp5],   0x70(%[ptmp])                           \n\t"
        PTR_ADDU   "%[addr1],   %[addr1],       %[addr5]                \n\t"
        PTR_ADDU   "%[addr4],   %[addr4],       %[addr5]                \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[stride]               \n\t"
        "gsldlc1    %[ftmp0],   0x07(%[addr1])                          \n\t"
        "gsldrc1    %[ftmp0],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr1],       %[addr0]                \n\t"
        "gsldlc1    %[ftmp1],   0x07(%[addr5])                          \n\t"
        "gsldrc1    %[ftmp1],   0x00(%[addr5])                          \n\t"
        "gsldlc1    %[ftmp2],   0x07(%[addr6])                          \n\t"
        "gsldrc1    %[ftmp2],   0x00(%[addr6])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[stride]               \n\t"
        "gsldlc1    %[ftmp3],   0x07(%[addr4])                          \n\t"
        "gsldrc1    %[ftmp3],   0x00(%[addr4])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr4],       %[addr0]                \n\t"
        "gsldlc1    %[ftmp4],   0x07(%[addr5])                          \n\t"
        "gsldrc1    %[ftmp4],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr2]                \n\t"
        "gsldlc1    %[ftmp5],   0x07(%[addr6])                          \n\t"
        "gsldrc1    %[ftmp5],   0x00(%[addr6])                          \n\t"
        "gsldlc1    %[ftmp6],   0x07(%[addr5])                          \n\t"
        "gsldrc1    %[ftmp6],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr3]                \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpckhbh  %[ftmp1],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "gsldlc1    %[ftmp8],   0x07(%[addr5])                          \n\t"
        "gsldrc1    %[ftmp8],   0x00(%[addr5])                          \n\t"
        "punpckhbh  %[ftmp5],   %[ftmp6],       %[ftmp8]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "sdc1       %[ftmp3],   0x08(%[ptmp])                           \n\t"
        "punpckhhw  %[ftmp3],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhhw  %[ftmp2],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpcklhw  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpckhhw  %[ftmp6],   %[ftmp7],       %[ftmp1]                \n\t"
        "punpcklhw  %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "sdc1       %[ftmp2],   0x28(%[ptmp])                           \n\t"
        "ldc1       %[ftmp2],   0x08(%[ptmp])                           \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp2],       %[ftmp5]                \n\t"
        "punpcklhw  %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpcklwd  %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpckhwd  %[ftmp4],   %[ftmp7],       %[ftmp2]                \n\t"
        "punpcklwd  %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
        "sdc1       %[ftmp0],   0x08(%[ptmp])                           \n\t"
        "sdc1       %[ftmp5],   0x18(%[ptmp])                           \n\t"
        "sdc1       %[ftmp7],   0x48(%[ptmp])                           \n\t"
        "sdc1       %[ftmp4],   0x58(%[ptmp])                           \n\t"
        "ldc1       %[ftmp8],   0x28(%[ptmp])                           \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp3],       %[ftmp8]                \n\t"
        "punpcklwd  %[ftmp3],   %[ftmp3],       %[ftmp8]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp6],       %[ftmp1]                \n\t"
        "punpcklwd  %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        "sdc1       %[ftmp3],   0x28(%[ptmp])                           \n\t"
        "sdc1       %[ftmp0],   0x38(%[ptmp])                           \n\t"
        "sdc1       %[ftmp6],   0x68(%[ptmp])                           \n\t"
        "sdc1       %[ftmp5],   0x78(%[ptmp])                           \n\t"
        PTR_S      "%[addr1],   0x00(%[pdat])                           \n\t"
        PTR_S      "%[addr2],   0x08(%[pdat])                           \n\t"
        PTR_S      "%[addr0],   0x10(%[pdat])                           \n\t"
        PTR_S      "%[addr3],   0x18(%[pdat])                           \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),            [addr5]"=&r"(addr[5]),
          [addr6]"=&r"(addr[6])
        : [pix]"r"(pix),                    [stride]"r"((mips_reg)stride),
          [ptmp]"r"(ptmp),                  [pdat]"r"(pdat)
        : "memory"
    );

    ff_deblock_v_luma_intra_8_mmi((uint8_t *) &ptmp[8], 0x10, alpha, beta);

    __asm__ volatile (
        PTR_L      "%[addr1],   0x00(%[pdat])                           \n\t"
        PTR_L      "%[addr2],   0x08(%[pdat])                           \n\t"
        PTR_L      "%[addr0],   0x10(%[pdat])                           \n\t"
        PTR_L      "%[addr3],   0x18(%[pdat])                           \n\t"
        PTR_ADDU   "%[addr4],   %[addr1],       %[addr2]                \n\t"
        "ldc1       %[ftmp0],   0x08(%[ptmp])                           \n\t"
        "ldc1       %[ftmp1],   0x18(%[ptmp])                           \n\t"
        "ldc1       %[ftmp2],   0x28(%[ptmp])                           \n\t"
        "ldc1       %[ftmp3],   0x38(%[ptmp])                           \n\t"
        "ldc1       %[ftmp4],   0x48(%[ptmp])                           \n\t"
        "ldc1       %[ftmp5],   0x58(%[ptmp])                           \n\t"
        "ldc1       %[ftmp6],   0x68(%[ptmp])                           \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpckhbh  %[ftmp1],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "ldc1       %[ftmp8],   0x78(%[ptmp])                           \n\t"
        "punpckhbh  %[ftmp5],   %[ftmp6],       %[ftmp8]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "gssdlc1    %[ftmp3],   0x07(%[addr1])                          \n\t"
        "gssdrc1    %[ftmp3],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[addr0]                \n\t"
        "punpckhhw  %[ftmp3],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhhw  %[ftmp2],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpcklhw  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpckhhw  %[ftmp6],   %[ftmp7],       %[ftmp1]                \n\t"
        "punpcklhw  %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "gssdlc1    %[ftmp2],   0x07(%[addr5])                          \n\t"
        "gssdrc1    %[ftmp2],   0x00(%[addr5])                          \n\t"
        "gsldlc1    %[ftmp2],   0x07(%[addr1])                          \n\t"
        "gsldrc1    %[ftmp2],   0x00(%[addr1])                          \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp2],       %[ftmp5]                \n\t"
        "punpcklhw  %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpcklwd  %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpckhwd  %[ftmp4],   %[ftmp7],       %[ftmp2]                \n\t"
        "punpcklwd  %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[stride]               \n\t"
        "gssdlc1    %[ftmp0],   0x07(%[addr1])                          \n\t"
        "gssdrc1    %[ftmp0],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr4],       %[stride]               \n\t"
        "gssdlc1    %[ftmp5],   0x07(%[addr5])                          \n\t"
        "gssdrc1    %[ftmp5],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr0]                \n\t"
        "gssdlc1    %[ftmp7],   0x07(%[addr6])                          \n\t"
        "gssdrc1    %[ftmp7],   0x00(%[addr6])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr1],       %[addr0]                \n\t"
        "gssdlc1    %[ftmp4],   0x07(%[addr5])                          \n\t"
        "gssdrc1    %[ftmp4],   0x00(%[addr5])                          \n\t"
        "gsldlc1    %[ftmp8],   0x07(%[addr6])                          \n\t"
        "gsldrc1    %[ftmp8],   0x00(%[addr6])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[addr0]                \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp3],       %[ftmp8]                \n\t"
        "punpcklwd  %[ftmp3],   %[ftmp3],       %[ftmp8]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp6],       %[ftmp1]                \n\t"
        "punpcklwd  %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        "gssdlc1    %[ftmp3],   0x07(%[addr5])                          \n\t"
        "gssdrc1    %[ftmp3],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr2]                \n\t"
        "gssdlc1    %[ftmp0],   0x07(%[addr4])                          \n\t"
        "gssdrc1    %[ftmp0],   0x00(%[addr4])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr4],       %[addr3]                \n\t"
        "gssdlc1    %[ftmp6],   0x07(%[addr5])                          \n\t"
        "gssdrc1    %[ftmp6],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr3],       %[addr3]                \n\t"
        "gssdlc1    %[ftmp5],   0x07(%[addr6])                          \n\t"
        "gssdrc1    %[ftmp5],   0x00(%[addr6])                          \n\t"
        PTR_SUBU   "%[addr1],   %[addr1],       %[addr5]                \n\t"
        PTR_SUBU   "%[addr4],   %[addr4],       %[addr5]                \n\t"
        "ldc1       %[ftmp0],   0x00(%[ptmp])                           \n\t"
        "ldc1       %[ftmp1],   0x10(%[ptmp])                           \n\t"
        "ldc1       %[ftmp2],   0x20(%[ptmp])                           \n\t"
        "ldc1       %[ftmp3],   0x30(%[ptmp])                           \n\t"
        "ldc1       %[ftmp4],   0x40(%[ptmp])                           \n\t"
        "ldc1       %[ftmp5],   0x50(%[ptmp])                           \n\t"
        "ldc1       %[ftmp6],   0x60(%[ptmp])                           \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "punpckhbh  %[ftmp1],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp3]                \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp4],       %[ftmp5]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp5]                \n\t"
        "ldc1       %[ftmp8],   0x70(%[ptmp])                           \n\t"
        "punpckhbh  %[ftmp5],   %[ftmp6],       %[ftmp8]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "gssdlc1    %[ftmp3],   0x07(%[addr1])                          \n\t"
        "gssdrc1    %[ftmp3],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[addr0]                \n\t"
        "punpckhhw  %[ftmp3],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpcklhw  %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "punpckhhw  %[ftmp2],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpcklhw  %[ftmp4],   %[ftmp4],       %[ftmp6]                \n\t"
        "punpckhhw  %[ftmp6],   %[ftmp7],       %[ftmp1]                \n\t"
        "punpcklhw  %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "gssdlc1    %[ftmp2],   0x07(%[addr5])                          \n\t"
        "gssdrc1    %[ftmp2],   0x00(%[addr5])                          \n\t"
        "gsldlc1    %[ftmp2],   0x07(%[addr1])                          \n\t"
        "gsldrc1    %[ftmp2],   0x00(%[addr1])                          \n\t"
        "punpckhhw  %[ftmp1],   %[ftmp2],       %[ftmp5]                \n\t"
        "punpcklhw  %[ftmp2],   %[ftmp2],       %[ftmp5]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpcklwd  %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpckhwd  %[ftmp4],   %[ftmp7],       %[ftmp2]                \n\t"
        "punpcklwd  %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[stride]               \n\t"
        "gssdlc1    %[ftmp0],   0x07(%[addr1])                          \n\t"
        "gssdrc1    %[ftmp0],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr4],       %[stride]               \n\t"
        "gssdlc1    %[ftmp5],   0x07(%[addr5])                          \n\t"
        "gssdrc1    %[ftmp5],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr0]                \n\t"
        "gssdlc1    %[ftmp7],   0x07(%[addr6])                          \n\t"
        "gssdrc1    %[ftmp7],   0x00(%[addr6])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr1],       %[addr0]                \n\t"
        "gssdlc1    %[ftmp4],   0x07(%[addr5])                          \n\t"
        "gssdrc1    %[ftmp4],   0x00(%[addr5])                          \n\t"
        "gsldlc1    %[ftmp8],   0x07(%[addr6])                          \n\t"
        "gsldrc1    %[ftmp8],   0x00(%[addr6])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr1],       %[addr0]                \n\t"
        "punpckhwd  %[ftmp0],   %[ftmp3],       %[ftmp8]                \n\t"
        "punpcklwd  %[ftmp3],   %[ftmp3],       %[ftmp8]                \n\t"
        "punpckhwd  %[ftmp5],   %[ftmp6],       %[ftmp1]                \n\t"
        "punpcklwd  %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        "gssdlc1    %[ftmp3],   0x07(%[addr5])                          \n\t"
        "gssdrc1    %[ftmp3],   0x00(%[addr5])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr4],       %[addr2]                \n\t"
        "gssdlc1    %[ftmp0],   0x07(%[addr4])                          \n\t"
        "gssdrc1    %[ftmp0],   0x00(%[addr4])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr4],       %[addr3]                \n\t"
        "gssdlc1    %[ftmp6],   0x07(%[addr5])                          \n\t"
        "gssdrc1    %[ftmp6],   0x00(%[addr5])                          \n\t"
        "gssdlc1    %[ftmp5],   0x07(%[addr6])                          \n\t"
        "gssdrc1    %[ftmp5],   0x00(%[addr6])                          \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),            [addr5]"=&r"(addr[5]),
          [addr6]"=&r"(addr[6])
        : [pix]"r"(pix),                    [stride]"r"((mips_reg)stride),
          [ptmp]"r"(ptmp),                  [pdat]"r"(pdat)
        : "memory"
    );
}
