/*
 * Loongson SIMD optimized h264pred
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

#include "h264pred_mips.h"
#include "libavcodec/bit_depth_template.c"
#include "libavutil/mips/mmiutils.h"
#include "constants.h"

void ff_pred16x16_vertical_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    double ftmp[2];
    uint64_t tmp[1];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "dli        %[tmp0],    0x08                                    \n\t"
        MMI_LDC1(%[ftmp0], %[srcA], 0x00)
        MMI_LDC1(%[ftmp1], %[srcA], 0x08)

        "1:                                                             \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        MMI_SDC1(%[ftmp1], %[src], 0x08)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        MMI_SDC1(%[ftmp1], %[src], 0x08)

        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [src]"+&r"(src)
        : [stride]"r"((mips_reg)stride),    [srcA]"r"((mips_reg)(src-stride))
        : "memory"
    );
}

void ff_pred16x16_horizontal_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    uint64_t tmp[3];
    mips_reg addr[2];

    __asm__ volatile (
        PTR_ADDI   "%[addr0],   %[src],         -0x01                   \n\t"
        PTR_ADDU   "%[addr1],   %[src],         $0                      \n\t"
        "dli        %[tmp2],    0x08                                    \n\t"
        "1:                                                             \n\t"
        "lbu        %[tmp0],    0x00(%[addr0])                          \n\t"
        "dmul       %[tmp1],    %[tmp0],        %[ff_pb_1]              \n\t"
        "swl        %[tmp1],    0x07(%[addr1])                          \n\t"
        "swr        %[tmp1],    0x00(%[addr1])                          \n\t"
        "swl        %[tmp1],    0x0f(%[addr1])                          \n\t"
        "swr        %[tmp1],    0x08(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        PTR_ADDU   "%[addr1],   %[addr1],       %[stride]               \n\t"
        "lbu        %[tmp0],    0x00(%[addr0])                          \n\t"
        "dmul       %[tmp1],    %[tmp0],        %[ff_pb_1]              \n\t"
        "swl        %[tmp1],    0x07(%[addr1])                          \n\t"
        "swr        %[tmp1],    0x00(%[addr1])                          \n\t"
        "swl        %[tmp1],    0x0f(%[addr1])                          \n\t"
        "swr        %[tmp1],    0x08(%[addr1])                          \n\t"
        "daddi      %[tmp2],    %[tmp2],        -0x01                   \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        PTR_ADDU   "%[addr1],   %[addr1],       %[stride]               \n\t"
        "bnez       %[tmp2],    1b                                      \n\t"
        : [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          [tmp2]"=&r"(tmp[2]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1])
        : [src]"r"((mips_reg)src),          [stride]"r"((mips_reg)stride),
          [ff_pb_1]"r"(ff_pb_1)
        : "memory"
    );
}

void ff_pred16x16_dc_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    uint64_t tmp[4];
    mips_reg addr[2];

    __asm__ volatile (
        PTR_ADDI   "%[addr0],   %[src],         -0x01                   \n\t"
        "dli        %[tmp0],    0x08                                    \n\t"
        "xor        %[tmp3],    %[tmp3],        %[tmp3]                 \n\t"
        "1:                                                             \n\t"
        "lbu        %[tmp1],    0x00(%[addr0])                          \n\t"
        "daddu      %[tmp3],    %[tmp3],        %[tmp1]                 \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp1],    0x00(%[addr0])                          \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        "daddu      %[tmp3],    %[tmp3],        %[tmp1]                 \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"

        "dli        %[tmp0],    0x08                                    \n\t"
        PTR_SUBU   "%[addr0],   %[src],         %[stride]               \n\t"
        "2:                                                             \n\t"
        "lbu        %[tmp1],    0x00(%[addr0])                          \n\t"
        "daddu      %[tmp3],    %[tmp3],        %[tmp1]                 \n\t"
        PTR_ADDIU  "%[addr0],   %[addr0],       0x01                    \n\t"
        "lbu        %[tmp1],    0x00(%[addr0])                          \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        "daddu      %[tmp3],    %[tmp3],        %[tmp1]                 \n\t"
        PTR_ADDIU  "%[addr0],   %[addr0],       0x01                    \n\t"
        "bnez       %[tmp0],    2b                                      \n\t"

        "daddiu     %[tmp3],    %[tmp3],        0x10                    \n\t"
        "dsra       %[tmp3],    0x05                                    \n\t"
        "dmul       %[tmp2],    %[tmp3],        %[ff_pb_1]              \n\t"
        PTR_ADDU   "%[addr0],   %[src],         $0                      \n\t"
        "dli        %[tmp0],    0x08                                    \n\t"
        "3:                                                             \n\t"
        "swl        %[tmp2],    0x07(%[addr0])                          \n\t"
        "swr        %[tmp2],    0x00(%[addr0])                          \n\t"
        "swl        %[tmp2],    0x0f(%[addr0])                          \n\t"
        "swr        %[tmp2],    0x08(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "swl        %[tmp2],    0x07(%[addr0])                          \n\t"
        "swr        %[tmp2],    0x00(%[addr0])                          \n\t"
        "swl        %[tmp2],    0x0f(%[addr0])                          \n\t"
        "swr        %[tmp2],    0x08(%[addr0])                          \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "bnez       %[tmp0],    3b                                      \n\t"
        : [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          [tmp2]"=&r"(tmp[2]),              [tmp3]"=&r"(tmp[3]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1])
        : [src]"r"((mips_reg)src),          [stride]"r"((mips_reg)stride),
          [ff_pb_1]"r"(ff_pb_1)
        : "memory"
    );
}

void ff_pred8x8l_top_dc_8_mmi(uint8_t *src, int has_topleft,
        int has_topright, ptrdiff_t stride)
{
    uint32_t dc;
    double ftmp[11];
    mips_reg tmp[3];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        MMI_ULDC1(%[ftmp10], %[srcA], 0x00)
        MMI_ULDC1(%[ftmp9], %[src0], 0x00)
        MMI_ULDC1(%[ftmp8], %[src1], 0x00)

        "punpcklbh  %[ftmp7],   %[ftmp10],      %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp6],   %[ftmp10],      %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp9],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp4],   %[ftmp9],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp8],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp2],   %[ftmp8],       %[ftmp0]                \n\t"
        "bnez       %[has_topleft],             1f                      \n\t"
        "pinsrh_0   %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"

        "1:                                                             \n\t"
        "bnez       %[has_topright],            2f                      \n\t"
        "dli        %[tmp0],    0xa4                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp1]                                \n\t"
        "pshufh     %[ftmp2],   %[ftmp2],       %[ftmp1]                \n\t"

        "2:                                                             \n\t"
        "dli        %[tmp0],    0x02                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp1]                                \n\t"
        "pmullh     %[ftmp5],   %[ftmp5],       %[ff_pw_2]              \n\t"
        "pmullh     %[ftmp4],   %[ftmp4],       %[ff_pw_2]              \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp5]                \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ftmp4]                \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp3]                \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]                \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ff_pw_2]              \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ff_pw_2]              \n\t"
        "psrah      %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        "packushb   %[ftmp9],   %[ftmp7],       %[ftmp6]                \n\t"
        "biadd      %[ftmp10],  %[ftmp9]                                \n\t"
        "mfc1       %[tmp1],    %[ftmp10]                               \n\t"
        "addiu      %[tmp1],    %[tmp1],        0x04                    \n\t"
        "srl        %[tmp1],    %[tmp1],        0x03                    \n\t"
        "mul        %[dc],      %[tmp1],        %[ff_pb_1]              \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),
          [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          RESTRICT_ASM_ALL64
          [dc]"=r"(dc)
        : [srcA]"r"((mips_reg)(src-stride-1)),
          [src0]"r"((mips_reg)(src-stride)),
          [src1]"r"((mips_reg)(src-stride+1)),
          [has_topleft]"r"(has_topleft),    [has_topright]"r"(has_topright),
          [ff_pb_1]"r"(ff_pb_1),            [ff_pw_2]"f"(ff_pw_2)
        : "memory"
    );

    __asm__ volatile (
        "dli        %[tmp0],    0x02                                    \n\t"
        "punpcklwd  %[ftmp0],   %[dc],          %[dc]                   \n\t"

        "1:                                                             \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        MMI_SDXC1(%[ftmp0], %[src], %[stride], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        MMI_SDXC1(%[ftmp0], %[src], %[stride], 0x00)

        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [src]"+&r"(src)
        : [dc]"f"(dc),                      [stride]"r"((mips_reg)stride)
        : "memory"
    );
}

void ff_pred8x8l_dc_8_mmi(uint8_t *src, int has_topleft, int has_topright,
        ptrdiff_t stride)
{
    uint32_t dc, dc1, dc2;
    double ftmp[14];
    mips_reg tmp[1];

    const int l0 = ((has_topleft ? src[-1+-1*stride] : src[-1+0*stride]) + 2*src[-1+0*stride] + src[-1+1*stride] + 2) >> 2;
    const int l1 = (src[-1+0*stride] + 2*src[-1+1*stride] + src[-1+2*stride] + 2) >> 2;
    const int l2 = (src[-1+1*stride] + 2*src[-1+2*stride] + src[-1+3*stride] + 2) >> 2;
    const int l3 = (src[-1+2*stride] + 2*src[-1+3*stride] + src[-1+4*stride] + 2) >> 2;
    const int l4 = (src[-1+3*stride] + 2*src[-1+4*stride] + src[-1+5*stride] + 2) >> 2;
    const int l5 = (src[-1+4*stride] + 2*src[-1+5*stride] + src[-1+6*stride] + 2) >> 2;
    const int l6 = (src[-1+5*stride] + 2*src[-1+6*stride] + src[-1+7*stride] + 2) >> 2;
    const int l7 = (src[-1+6*stride] + 2*src[-1+7*stride] + src[-1+7*stride] + 2) >> 2;

    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        MMI_ULDC1(%[ftmp4], %[srcA], 0x00)
        MMI_ULDC1(%[ftmp5], %[src0], 0x00)
        MMI_ULDC1(%[ftmp6], %[src1], 0x00)
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "dli        %[tmp0],    0x03                                    \n\t"
        "punpcklbh  %[ftmp7],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp4],       %[ftmp0]                \n\t"
        "mtc1       %[tmp0],    %[ftmp1]                                \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp10],  %[ftmp5],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp11],  %[ftmp6],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp12],  %[ftmp6],       %[ftmp0]                \n\t"
        "pshufh     %[ftmp3],   %[ftmp8],       %[ftmp1]                \n\t"
        "pshufh     %[ftmp13],  %[ftmp12],      %[ftmp1]                \n\t"
        "pinsrh_3   %[ftmp8],   %[ftmp8],       %[ftmp13]               \n\t"
        "pinsrh_3   %[ftmp12],  %[ftmp12],      %[ftmp3]                \n\t"
        "bnez       %[has_topleft],             1f                      \n\t"
        "pinsrh_0   %[ftmp7],   %[ftmp7],       %[ftmp9]                \n\t"

        "1:                                                             \n\t"
        "bnez       %[has_topright],            2f                      \n\t"
        "pshufh     %[ftmp13],  %[ftmp10],      %[ftmp1]                \n\t"
        "pinsrh_3   %[ftmp8],   %[ftmp8],       %[ftmp13]               \n\t"

        "2:                                                             \n\t"
        "dli        %[tmp0],    0x02                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp1]                                \n\t"
        "pshufh     %[ftmp2],   %[ftmp1],       %[ftmp0]                \n\t"
        "pmullh     %[ftmp9],   %[ftmp9],       %[ftmp2]                \n\t"
        "pmullh     %[ftmp10],  %[ftmp10],      %[ftmp2]                \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp9]                \n\t"
        "paddh      %[ftmp8],   %[ftmp8],       %[ftmp10]               \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp11]               \n\t"
        "paddh      %[ftmp8],   %[ftmp8],       %[ftmp12]               \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
        "paddh      %[ftmp8],   %[ftmp8],       %[ftmp2]                \n\t"
        "psrah      %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "psrah      %[ftmp8],   %[ftmp8],       %[ftmp1]                \n\t"
        "packushb   %[ftmp5],   %[ftmp7],       %[ftmp8]                \n\t"
        "biadd      %[ftmp4],   %[ftmp5]                                \n\t"
        "mfc1       %[dc2],     %[ftmp4]                                \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
          [ftmp12]"=&f"(ftmp[12]),          [ftmp13]"=&f"(ftmp[13]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [dc2]"=r"(dc2)
        : [srcA]"r"((mips_reg)(src-stride-1)),
          [src0]"r"((mips_reg)(src-stride)),
          [src1]"r"((mips_reg)(src-stride+1)),
          [has_topleft]"r"(has_topleft),    [has_topright]"r"(has_topright)
        : "memory"
    );

    dc1 = l0+l1+l2+l3+l4+l5+l6+l7;
    dc = ((dc1+dc2+8)>>4)*0x01010101U;

    __asm__ volatile (
        "dli        %[tmp0],    0x02                                    \n\t"
        "punpcklwd  %[ftmp0],   %[dc],          %[dc]                   \n\t"

        "1:                                                             \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        MMI_SDXC1(%[ftmp0], %[src], %[stride], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        MMI_SDXC1(%[ftmp0], %[src], %[stride], 0x00)

        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [src]"+&r"(src)
        : [dc]"f"(dc),                      [stride]"r"((mips_reg)stride)
        : "memory"
    );
}

void ff_pred8x8l_vertical_8_mmi(uint8_t *src, int has_topleft,
        int has_topright, ptrdiff_t stride)
{
    double ftmp[12];
    mips_reg tmp[1];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        MMI_LDC1(%[ftmp3], %[srcA], 0x00)
        MMI_LDC1(%[ftmp4], %[src0], 0x00)
        MMI_LDC1(%[ftmp5], %[src1], 0x00)
        "punpcklbh  %[ftmp6],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp8],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp9],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp10],  %[ftmp5],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp11],  %[ftmp5],       %[ftmp0]                \n\t"
        "bnez       %[has_topleft],             1f                      \n\t"
        "pinsrh_0   %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"

        "1:                                                             \n\t"
        "bnez       %[has_topright],            2f                      \n\t"
        "dli        %[tmp0],    0xa4                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp1]                                \n\t"
        "pshufh     %[ftmp11],  %[ftmp11],      %[ftmp1]                \n\t"

        "2:                                                             \n\t"
        "dli        %[tmp0],    0x02                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp1]                                \n\t"
        "pshufh     %[ftmp2],   %[ftmp1],       %[ftmp0]                \n\t"
        "pmullh     %[ftmp8],   %[ftmp8],       %[ftmp2]                \n\t"
        "pmullh     %[ftmp9],   %[ftmp9],       %[ftmp2]                \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ftmp8]                \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp9]                \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ftmp10]               \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp11]               \n\t"
        "paddh      %[ftmp6],   %[ftmp6],       %[ftmp2]                \n\t"
        "paddh      %[ftmp7],   %[ftmp7],       %[ftmp2]                \n\t"
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp1]                \n\t"
        "psrah      %[ftmp7],   %[ftmp7],       %[ftmp1]                \n\t"
        "packushb   %[ftmp4],   %[ftmp6],       %[ftmp7]                \n\t"
        MMI_SDC1(%[ftmp4], %[src], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [src]"=r"(src)
        : [srcA]"r"((mips_reg)(src-stride-1)),
          [src0]"r"((mips_reg)(src-stride)),
          [src1]"r"((mips_reg)(src-stride+1)),
          [has_topleft]"r"(has_topleft),    [has_topright]"r"(has_topright)
        : "memory"
    );

    __asm__ volatile (
        "dli        %[tmp0],    0x02                                    \n\t"

        "1:                                                             \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)

        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [src]"+&r"(src)
        : [stride]"r"((mips_reg)stride)
        : "memory"
    );
}

void ff_pred4x4_dc_8_mmi(uint8_t *src, const uint8_t *topright,
        ptrdiff_t stride)
{
    const int dc = (src[-stride] + src[1-stride] + src[2-stride]
                 + src[3-stride] + src[-1+0*stride] + src[-1+1*stride]
                 + src[-1+2*stride] + src[-1+3*stride] + 4) >>3;
    uint64_t tmp[2];
    mips_reg addr[1];
    DECLARE_VAR_ADDRT;

    __asm__ volatile (
        PTR_ADDU   "%[tmp0],    %[dc],          $0                      \n\t"
        "dmul       %[tmp1],    %[tmp0],        %[ff_pb_1]              \n\t"
        "xor        %[addr0],   %[addr0],       %[addr0]                \n\t"
        MMI_SWX(%[tmp1], %[src], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        MMI_SWX(%[tmp1], %[src], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        MMI_SWX(%[tmp1], %[src], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        MMI_SWX(%[tmp1], %[src], %[addr0], 0x00)
        : [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          RESTRICT_ASM_ADDRT
          [addr0]"=&r"(addr[0])
        : [src]"r"((mips_reg)src),          [stride]"r"((mips_reg)stride),
          [dc]"r"(dc),                      [ff_pb_1]"r"(ff_pb_1)
        : "memory"
    );
}

void ff_pred8x8_vertical_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    uint64_t tmp[2];
    mips_reg addr[2];

    __asm__ volatile (
        PTR_SUBU   "%[addr0],   %[src],         %[stride]               \n\t"
        PTR_ADDU   "%[addr1],   %[src],         $0                      \n\t"
        "ldl        %[tmp0],    0x07(%[addr0])                          \n\t"
        "ldr        %[tmp0],    0x00(%[addr0])                          \n\t"
        "dli        %[tmp1],    0x04                                    \n\t"
        "1:                                                             \n\t"
        "sdl        %[tmp0],    0x07(%[addr1])                          \n\t"
        "sdr        %[tmp0],    0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr1],   %[stride]                               \n\t"
        "sdl        %[tmp0],    0x07(%[addr1])                          \n\t"
        "sdr        %[tmp0],    0x00(%[addr1])                          \n\t"
        "daddi      %[tmp1],    -0x01                                   \n\t"
        PTR_ADDU   "%[addr1],   %[stride]                               \n\t"
        "bnez       %[tmp1],    1b                                      \n\t"
        : [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1])
        : [src]"r"((mips_reg)src),          [stride]"r"((mips_reg)stride)
        : "memory"
    );
}

void ff_pred8x8_horizontal_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    uint64_t tmp[3];
    mips_reg addr[2];

    __asm__ volatile (
        PTR_ADDI   "%[addr0],   %[src],         -0x01                   \n\t"
        PTR_ADDU   "%[addr1],   %[src],         $0                      \n\t"
        "dli        %[tmp0],    0x04                                    \n\t"
        "1:                                                             \n\t"
        "lbu        %[tmp1],    0x00(%[addr0])                          \n\t"
        "dmul       %[tmp2],    %[tmp1],        %[ff_pb_1]              \n\t"
        "swl        %[tmp2],    0x07(%[addr1])                          \n\t"
        "swr        %[tmp2],    0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        PTR_ADDU   "%[addr1],   %[addr1],       %[stride]               \n\t"
        "lbu        %[tmp1],    0x00(%[addr0])                          \n\t"
        "dmul       %[tmp2],    %[tmp1],        %[ff_pb_1]              \n\t"
        "swl        %[tmp2],    0x07(%[addr1])                          \n\t"
        "swr        %[tmp2],    0x00(%[addr1])                          \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        PTR_ADDU   "%[addr1],   %[addr1],       %[stride]               \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          [tmp2]"=&r"(tmp[2]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1])
        : [src]"r"((mips_reg)src),          [stride]"r"((mips_reg)stride),
          [ff_pb_1]"r"(ff_pb_1)
        : "memory"
    );
}

void ff_pred8x8_top_dc_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    double ftmp[4];
    uint64_t tmp[1];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "dli        %[tmp0],    0x02                                    \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        PTR_SUBU   "%[addr0],   %[src],         %[stride]               \n\t"
        MMI_LDC1(%[ftmp1], %[addr0], 0x00)
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]                \n\t"
        "biadd      %[ftmp2],   %[ftmp2]                                \n\t"
        "biadd      %[ftmp3],   %[ftmp3]                                \n\t"
        "mtc1       %[tmp0],    %[ftmp1]                                \n\t"
        "pshufh     %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "pshufh     %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "pshufh     %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "paddush    %[ftmp2],   %[ftmp2],       %[ftmp1]                \n\t"
        "paddush    %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "mtc1       %[tmp0],    %[ftmp1]                                \n\t"
        "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp1]                \n\t"
        "psrlh      %[ftmp3],   %[ftmp3],       %[ftmp1]                \n\t"
        "packushb   %[ftmp1],   %[ftmp2],       %[ftmp3]                \n\t"
        MMI_SDC1(%[ftmp1], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp1], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp1], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp1], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp1], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp1], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp1], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp1], %[src], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [addr0]"=&r"(addr[0]),
          [src]"+&r"(src)
        : [stride]"r"((mips_reg)stride)
        : "memory"
    );
}

void ff_pred8x8_dc_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    double ftmp[5];
    mips_reg addr[7];

    __asm__ volatile (
        "negu       %[addr0],   %[stride]                               \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[src]                  \n\t"
        PTR_ADDIU  "%[addr1],   %[addr0],       0x04                    \n\t"
        "lbu        %[addr2],   0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr3],   $0,             %[addr2]                \n\t"
        PTR_ADDIU  "%[addr0],   0x01                                    \n\t"
        "lbu        %[addr2],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr4],   $0,             %[addr2]                \n\t"
        PTR_ADDIU  "%[addr1],   0x01                                    \n\t"
        "lbu        %[addr2],   0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr3],       %[addr2]                \n\t"
        PTR_ADDIU  "%[addr0],   0x01                                    \n\t"
        "lbu        %[addr2],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr4],   %[addr4],       %[addr2]                \n\t"
        PTR_ADDIU  "%[addr1],   0x01                                    \n\t"
        "lbu        %[addr2],   0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr3],       %[addr2]                \n\t"
        PTR_ADDIU  "%[addr0],   0x01                                    \n\t"
        "lbu        %[addr2],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr4],   %[addr4],       %[addr2]                \n\t"
        PTR_ADDIU  "%[addr1],   0x01                                    \n\t"
        "lbu        %[addr2],   0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr3],   %[addr3],       %[addr2]                \n\t"
        PTR_ADDIU  "%[addr0],   0x01                                    \n\t"
        "lbu        %[addr2],   0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr4],   %[addr4],       %[addr2]                \n\t"
        PTR_ADDIU  "%[addr1],   0x01                                    \n\t"
        "dli        %[addr2],  -0x01                                    \n\t"
        PTR_ADDU   "%[addr2],   %[addr2],       %[src]                  \n\t"
        "lbu        %[addr1],   0x00(%[addr2])                          \n\t"
        PTR_ADDU   "%[addr5],   $0,             %[addr1]                \n\t"
        PTR_ADDU   "%[addr2],   %[addr2],       %[stride]               \n\t"
        "lbu        %[addr1],   0x00(%[addr2])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr5],       %[addr1]                \n\t"
        PTR_ADDU   "%[addr2],   %[addr2],       %[stride]               \n\t"
        "lbu        %[addr1],   0x00(%[addr2])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr5],       %[addr1]                \n\t"
        PTR_ADDU   "%[addr2],   %[addr2],       %[stride]               \n\t"
        "lbu        %[addr1],   0x00(%[addr2])                          \n\t"
        PTR_ADDU   "%[addr5],   %[addr5],       %[addr1]                \n\t"
        PTR_ADDU   "%[addr2],   %[addr2],       %[stride]               \n\t"
        "lbu        %[addr1],   0x00(%[addr2])                          \n\t"
        PTR_ADDU   "%[addr6],   $0,             %[addr1]                \n\t"
        PTR_ADDU   "%[addr2],   %[addr2],       %[stride]               \n\t"
        "lbu        %[addr1],   0x00(%[addr2])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr6],       %[addr1]                \n\t"
        PTR_ADDU   "%[addr2],   %[addr2],       %[stride]               \n\t"
        "lbu        %[addr1],   0x00(%[addr2])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr6],       %[addr1]                \n\t"
        PTR_ADDU   "%[addr2],   %[addr2],       %[stride]               \n\t"
        "lbu        %[addr1],   0x00(%[addr2])                          \n\t"
        PTR_ADDU   "%[addr6],   %[addr6],       %[addr1]                \n\t"
        PTR_ADDU   "%[addr3],   %[addr3],       %[addr5]                \n\t"
        PTR_ADDIU  "%[addr3],   %[addr3],       0x04                    \n\t"
        PTR_ADDIU  "%[addr4],   %[addr4],       0x02                    \n\t"
        PTR_ADDIU  "%[addr1],   %[addr6],       0x02                    \n\t"
        PTR_ADDU   "%[addr2],   %[addr4],       %[addr1]                \n\t"
        PTR_SRL    "%[addr3],   0x03                                    \n\t"
        PTR_SRL    "%[addr4],   0x02                                    \n\t"
        PTR_SRL    "%[addr1],   0x02                                    \n\t"
        PTR_SRL    "%[addr2],   0x03                                    \n\t"
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "dmtc1      %[addr3],   %[ftmp1]                                \n\t"
        "pshufh     %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "dmtc1      %[addr4],   %[ftmp2]                                \n\t"
        "pshufh     %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "dmtc1      %[addr1],   %[ftmp3]                                \n\t"
        "pshufh     %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "dmtc1      %[addr2],   %[ftmp4]                                \n\t"
        "pshufh     %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp2]                \n\t"
        "packushb   %[ftmp2],   %[ftmp3],       %[ftmp4]                \n\t"
        PTR_ADDU   "%[addr0],   $0,             %[src]                  \n\t"
        MMI_SDC1(%[ftmp1], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        MMI_SDC1(%[ftmp1], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        MMI_SDC1(%[ftmp1], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        MMI_SDC1(%[ftmp1], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        MMI_SDC1(%[ftmp2], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        MMI_SDC1(%[ftmp2], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        MMI_SDC1(%[ftmp2], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        MMI_SDC1(%[ftmp2], %[addr0], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [addr2]"=&r"(addr[2]),            [addr3]"=&r"(addr[3]),
          [addr4]"=&r"(addr[4]),            [addr5]"=&r"(addr[5]),
          [addr6]"=&r"(addr[6])
        : [src]"r"((mips_reg)src),          [stride]"r"((mips_reg)stride)
        : "memory"
    );
}

void ff_pred8x16_vertical_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    double ftmp[1];
    uint64_t tmp[1];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        MMI_LDC1(%[ftmp0], %[srcA], 0x00)
        "dli        %[tmp0],    0x04                                    \n\t"

        "1:                                                             \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        MMI_SDC1(%[ftmp0], %[src], 0x00)

        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[src],     %[src],         %[stride]               \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [src]"+&r"(src)
        : [stride]"r"((mips_reg)stride),    [srcA]"r"((mips_reg)(src-stride))
        : "memory"
    );
}

void ff_pred8x16_horizontal_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    uint64_t tmp[3];
    mips_reg addr[2];

    __asm__ volatile (
        PTR_ADDI   "%[addr0],   %[src],         -0x01                   \n\t"
        PTR_ADDU   "%[addr1],   %[src],         $0                      \n\t"
        "dli        %[tmp0],    0x08                                    \n\t"
        "1:                                                             \n\t"
        "lbu        %[tmp1],    0x00(%[addr0])                          \n\t"
        "dmul       %[tmp2],    %[tmp1],        %[ff_pb_1]              \n\t"
        "swl        %[tmp2],    0x07(%[addr1])                          \n\t"
        "swr        %[tmp2],    0x00(%[addr1])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        PTR_ADDU   "%[addr1],   %[addr1],       %[stride]               \n\t"
        "lbu        %[tmp1],    0x00(%[addr0])                          \n\t"
        "dmul       %[tmp2],    %[tmp1],        %[ff_pb_1]              \n\t"
        "swl        %[tmp2],    0x07(%[addr1])                          \n\t"
        "swr        %[tmp2],    0x00(%[addr1])                          \n\t"
        "daddi      %[tmp0],    %[tmp0],        -0x01                   \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        PTR_ADDU   "%[addr1],   %[addr1],       %[stride]               \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          [tmp2]"=&r"(tmp[2]),
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1])
        : [src]"r"((mips_reg)src),          [stride]"r"((mips_reg)stride),
          [ff_pb_1]"r"(ff_pb_1)
        : "memory"
    );
}

static inline void pred16x16_plane_compat_mmi(uint8_t *src, int stride,
        const int svq3, const int rv40)
{
    double ftmp[11];
    uint64_t tmp[6];
    mips_reg addr[1];
    DECLARE_VAR_ALL64;

    __asm__ volatile(
        PTR_SUBU   "%[addr0],   %[src],         %[stride]               \n\t"
        "dli        %[tmp0],    0x20                                    \n\t"
        "dmtc1      %[tmp0],    %[ftmp4]                                \n\t"
        MMI_ULDC1(%[ftmp0], %[addr0], -0x01)
        MMI_ULDC1(%[ftmp2], %[addr0],  0x08)
        "dsrl       %[ftmp1],   %[ftmp0],       %[ftmp4]                \n\t"
        "dsrl       %[ftmp3],   %[ftmp2],       %[ftmp4]                \n\t"
        "xor        %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        "punpcklbh  %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp4]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp4]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp4]                \n\t"
        "pmullh     %[ftmp0],   %[ftmp0],       %[ff_pw_m8tom5]         \n\t"
        "pmullh     %[ftmp1],   %[ftmp1],       %[ff_pw_m4tom1]         \n\t"
        "pmullh     %[ftmp2],   %[ftmp2],       %[ff_pw_1to4]           \n\t"
        "pmullh     %[ftmp3],   %[ftmp3],       %[ff_pw_5to8]           \n\t"
        "paddsh     %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "paddsh     %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "dli        %[tmp0],    0x0e                                    \n\t"
        "dmtc1      %[tmp0],    %[ftmp4]                                \n\t"
        "pshufh     %[ftmp1],   %[ftmp0],       %[ftmp4]                \n\t"
        "paddsh     %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "dli        %[tmp0],    0x01                                    \n\t"
        "dmtc1      %[tmp0],    %[ftmp4]                                \n\t"
        "pshufh     %[ftmp1],   %[ftmp0],       %[ftmp4]                \n\t"
        "paddsh     %[ftmp5],   %[ftmp0],       %[ftmp1]                \n\t"

        PTR_ADDIU  "%[addr0],   %[src],         -0x01                   \n\t"
        PTR_SUBU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp2],    0x00(%[addr0])                          \n\t"
        "lbu        %[tmp5],    0x10(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp3],    0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp4],    0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp0],    0x00(%[addr0])                          \n\t"
        "dsll       %[tmp3],    %[tmp3],        0x10                    \n\t"
        "dsll       %[tmp4],    %[tmp4],        0x20                    \n\t"
        "dsll       %[tmp0],    %[tmp0],        0x30                    \n\t"
        "or         %[tmp4],    %[tmp4],        %[tmp0]                 \n\t"
        "or         %[tmp2],    %[tmp2],        %[tmp3]                 \n\t"
        "or         %[tmp2],    %[tmp2],        %[tmp4]                 \n\t"
        "dmtc1      %[tmp2],    %[ftmp0]                                \n\t"

        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp2],    0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp3],    0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp4],    0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp0],    0x00(%[addr0])                          \n\t"
        "dsll       %[tmp3],    %[tmp3],        0x10                    \n\t"
        "dsll       %[tmp4],    %[tmp4],        0x20                    \n\t"
        "dsll       %[tmp0],    %[tmp0],        0x30                    \n\t"
        "or         %[tmp4],    %[tmp4],        %[tmp0]                 \n\t"
        "or         %[tmp2],    %[tmp2],        %[tmp3]                 \n\t"
        "or         %[tmp2],    %[tmp2],        %[tmp4]                 \n\t"
        "dmtc1      %[tmp2],    %[ftmp1]                                \n\t"

        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp2],    0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp3],    0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp4],    0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp0],    0x00(%[addr0])                          \n\t"
        "dsll       %[tmp3],    %[tmp3],        0x10                    \n\t"
        "dsll       %[tmp4],    %[tmp4],        0x20                    \n\t"
        "dsll       %[tmp0],    %[tmp0],        0x30                    \n\t"
        "or         %[tmp4],    %[tmp4],        %[tmp0]                 \n\t"
        "or         %[tmp2],    %[tmp2],        %[tmp3]                 \n\t"
        "or         %[tmp2],    %[tmp2],        %[tmp4]                 \n\t"
        "dmtc1      %[tmp2],    %[ftmp2]                                \n\t"

        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp2],    0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp3],    0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp4],    0x00(%[addr0])                          \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "lbu        %[tmp0],    0x00(%[addr0])                          \n\t"
        "daddu      %[tmp5],    %[tmp5],        %[tmp0]                 \n\t"
        "daddiu     %[tmp5],    %[tmp5],        0x01                    \n\t"
        "dsll       %[tmp5],    %[tmp5],        0x04                    \n\t"

        "dsll       %[tmp3],    %[tmp3],        0x10                    \n\t"
        "dsll       %[tmp4],    %[tmp4],        0x20                    \n\t"
        "dsll       %[tmp0],    %[tmp0],        0x30                    \n\t"
        "or         %[tmp4],    %[tmp4],        %[tmp0]                 \n\t"
        "or         %[tmp2],    %[tmp2],        %[tmp3]                 \n\t"
        "or         %[tmp2],    %[tmp2],        %[tmp4]                 \n\t"
        "dmtc1      %[tmp2],    %[ftmp3]                                \n\t"

        "pmullh     %[ftmp0],   %[ftmp0],       %[ff_pw_m8tom5]         \n\t"
        "pmullh     %[ftmp1],   %[ftmp1],       %[ff_pw_m4tom1]         \n\t"
        "pmullh     %[ftmp2],   %[ftmp2],       %[ff_pw_1to4]           \n\t"
        "pmullh     %[ftmp3],   %[ftmp3],       %[ff_pw_5to8]           \n\t"
        "paddsh     %[ftmp0],   %[ftmp0],       %[ftmp2]                \n\t"
        "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp3]                \n\t"
        "paddsh     %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"
        "dli        %[tmp0],    0x0e                                    \n\t"
        "dmtc1      %[tmp0],    %[ftmp4]                                \n\t"
        "pshufh     %[ftmp1],   %[ftmp0],       %[ftmp4]                \n\t"
        "paddsh     %[ftmp0],   %[ftmp0],       %[ftmp1]                \n\t"

        "dli        %[tmp0],    0x01                                    \n\t"
        "dmtc1      %[tmp0],    %[ftmp4]                                \n\t"
        "pshufh     %[ftmp1],   %[ftmp0],       %[ftmp4]                \n\t"
        "paddsh     %[ftmp6],   %[ftmp0],       %[ftmp1]                \n\t"

        "dmfc1      %[tmp0],    %[ftmp5]                                \n\t"
        "dsll       %[tmp0],    %[tmp0],        0x30                    \n\t"
        "dsra       %[tmp0],    %[tmp0],        0x30                    \n\t"
        "dmfc1      %[tmp1],    %[ftmp6]                                \n\t"
        "dsll       %[tmp1],    %[tmp1],        0x30                    \n\t"
        "dsra       %[tmp1],    %[tmp1],        0x30                    \n\t"

        "beqz       %[svq3],    1f                                      \n\t"
        "dli        %[tmp2],    0x04                                    \n\t"
        "ddiv       %[tmp0],    %[tmp0],        %[tmp2]                 \n\t"
        "ddiv       %[tmp1],    %[tmp1],        %[tmp2]                 \n\t"
        "dli        %[tmp2],    0x05                                    \n\t"
        "dmul       %[tmp0],    %[tmp0],        %[tmp2]                 \n\t"
        "dmul       %[tmp1],    %[tmp1],        %[tmp2]                 \n\t"
        "dli        %[tmp2],    0x10                                    \n\t"
        "ddiv       %[tmp0],    %[tmp0],        %[tmp2]                 \n\t"
        "ddiv       %[tmp1],    %[tmp1],        %[tmp2]                 \n\t"
        "daddu      %[tmp2],    %[tmp0],        $0                      \n\t"
        "daddu      %[tmp0],    %[tmp1],        $0                      \n\t"
        "daddu      %[tmp1],    %[tmp2],        $0                      \n\t"
        "b          2f                                                  \n\t"

        "1:                                                             \n\t"
        "beqz       %[rv40],    1f                                      \n\t"
        "dsra       %[tmp2],    %[tmp0],        0x02                    \n\t"
        "daddu      %[tmp0],    %[tmp0],        %[tmp2]                 \n\t"
        "dsra       %[tmp2],    %[tmp1],        0x02                    \n\t"
        "daddu      %[tmp1],    %[tmp1],        %[tmp2]                 \n\t"
        "dsra       %[tmp0],    %[tmp0],        0x04                    \n\t"
        "dsra       %[tmp1],    %[tmp1],        0x04                    \n\t"
        "b          2f                                                  \n\t"

        "1:                                                             \n\t"
        "dli        %[tmp2],    0x05                                    \n\t"
        "dmul       %[tmp0],    %[tmp0],        %[tmp2]                 \n\t"
        "dmul       %[tmp1],    %[tmp1],        %[tmp2]                 \n\t"
        "daddiu     %[tmp0],    %[tmp0],        0x20                    \n\t"
        "daddiu     %[tmp1],    %[tmp1],        0x20                    \n\t"
        "dsra       %[tmp0],    %[tmp0],        0x06                    \n\t"
        "dsra       %[tmp1],    %[tmp1],        0x06                    \n\t"

        "2:                                                             \n\t"
        "daddu      %[tmp3],    %[tmp0],        %[tmp1]                 \n\t"
        "dli        %[tmp2],    0x07                                    \n\t"
        "dmul       %[tmp3],    %[tmp3],        %[tmp2]                 \n\t"
        "dsubu      %[tmp5],    %[tmp5],        %[tmp3]                 \n\t"

        "xor        %[ftmp4],   %[ftmp4],       %[ftmp4]                \n\t"
        "dmtc1      %[tmp0],    %[ftmp0]                                \n\t"
        "pshufh     %[ftmp0],   %[ftmp0],       %[ftmp4]                \n\t"
        "dmtc1      %[tmp1],    %[ftmp5]                                \n\t"
        "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp4]                \n\t"
        "dmtc1      %[tmp5],    %[ftmp6]                                \n\t"
        "pshufh     %[ftmp6],   %[ftmp6],       %[ftmp4]                \n\t"
        "dli        %[tmp0],    0x05                                    \n\t"
        "dmtc1      %[tmp0],    %[ftmp7]                                \n\t"
        "pmullh     %[ftmp1],   %[ff_pw_0to3],  %[ftmp0]                \n\t"
        "dmtc1      %[ff_pw_4to7],              %[ftmp2]                \n\t"
        "pmullh     %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "dmtc1      %[ff_pw_8tob],              %[ftmp3]                \n\t"
        "pmullh     %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "dmtc1      %[ff_pw_ctof],              %[ftmp4]                \n\t"
        "pmullh     %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"

        "dli        %[tmp0],    0x10                                    \n\t"
        PTR_ADDU   "%[addr0],   %[src],         $0                      \n\t"
        "1:                                                             \n\t"
        "paddsh     %[ftmp8],   %[ftmp1],       %[ftmp6]                \n\t"
        "psrah      %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp2],       %[ftmp6]                \n\t"
        "psrah      %[ftmp9],   %[ftmp9],       %[ftmp7]                \n\t"
        "packushb   %[ftmp0],   %[ftmp8],       %[ftmp9]                \n\t"
        MMI_SDC1(%[ftmp0], %[addr0], 0x00)

        "paddsh     %[ftmp8],   %[ftmp3],       %[ftmp6]                \n\t"
        "psrah      %[ftmp8],   %[ftmp8],       %[ftmp7]                \n\t"
        "paddsh     %[ftmp9],   %[ftmp4],       %[ftmp6]                \n\t"
        "psrah      %[ftmp9],   %[ftmp9],       %[ftmp7]                \n\t"
        "packushb   %[ftmp0],   %[ftmp8],       %[ftmp9]                \n\t"
        MMI_SDC1(%[ftmp0], %[addr0], 0x08)

        "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp5]                \n\t"
        PTR_ADDU   "%[addr0],   %[addr0],       %[stride]               \n\t"
        "daddiu     %[tmp0],    %[tmp0],        -0x01                   \n\t"
        "bnez       %[tmp0],    1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          [tmp2]"=&r"(tmp[2]),              [tmp3]"=&r"(tmp[3]),
          [tmp4]"=&r"(tmp[4]),              [tmp5]"=&r"(tmp[5]),
          RESTRICT_ASM_ALL64
          [addr0]"=&r"(addr[0])
        : [src]"r"(src),                    [stride]"r"((mips_reg)stride),
          [svq3]"r"(svq3),                  [rv40]"r"(rv40),
          [ff_pw_m8tom5]"f"(ff_pw_m8tom5),  [ff_pw_m4tom1]"f"(ff_pw_m4tom1),
          [ff_pw_1to4]"f"(ff_pw_1to4),      [ff_pw_5to8]"f"(ff_pw_5to8),
          [ff_pw_0to3]"f"(ff_pw_0to3),      [ff_pw_4to7]"r"(ff_pw_4to7),
          [ff_pw_8tob]"r"(ff_pw_8tob),      [ff_pw_ctof]"r"(ff_pw_ctof)
        : "memory"
    );
}

void ff_pred16x16_plane_h264_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    pred16x16_plane_compat_mmi(src, stride, 0, 0);
}

void ff_pred16x16_plane_svq3_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    pred16x16_plane_compat_mmi(src, stride, 1, 0);
}

void ff_pred16x16_plane_rv40_8_mmi(uint8_t *src, ptrdiff_t stride)
{
    pred16x16_plane_compat_mmi(src, stride, 0, 1);
}
