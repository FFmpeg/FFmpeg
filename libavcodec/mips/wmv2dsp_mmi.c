/*
 * WMV2 - DSP functions Loongson MMI-optimized
 *
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

#include "libavutil/avassert.h"
#include "constants.h"
#include "wmv2dsp_mips.h"
#include "libavutil/mips/mmiutils.h"

#define W0 2048
#define W1 2841 /* 2048*sqrt (2)*cos (1*pi/16) */
#define W2 2676 /* 2048*sqrt (2)*cos (2*pi/16) */
#define W3 2408 /* 2048*sqrt (2)*cos (3*pi/16) */
#define W4 2048 /* 2048*sqrt (2)*cos (4*pi/16) */
#define W5 1609 /* 2048*sqrt (2)*cos (5*pi/16) */
#define W6 1108 /* 2048*sqrt (2)*cos (6*pi/16) */
#define W7 565  /* 2048*sqrt (2)*cos (7*pi/16) */

static void wmv2_idct_row_mmi(short * b)
{
    int s1, s2;
    int a0, a1, a2, a3, a4, a5, a6, a7;

    /* step 1 */
    a0 = W0 * b[0] + W0 * b[4];
    a1 = W1 * b[1] + W7 * b[7];
    a2 = W2 * b[2] + W6 * b[6];
    a3 = W3 * b[5] - W5 * b[3];
    a4 = W0 * b[0] - W0 * b[4];
    a5 = W5 * b[5] + W3 * b[3];
    a6 = W6 * b[2] - W2 * b[6];
    a7 = W7 * b[1] - W1 * b[7];

    /* step 2 */
    s1 = (181 * (a1 - a5 + a7 - a3) + 128) >> 8; // 1, 3, 5, 7
    s2 = (181 * (a1 - a5 - a7 + a3) + 128) >> 8;

    /* step 3 */
    b[0] = (a0 + a2 + a1 + a5 + 128) >> 8;
    b[1] = (a4 + a6 + s1      + 128) >> 8;
    b[2] = (a4 - a6 + s2      + 128) >> 8;
    b[3] = (a0 - a2 + a7 + a3 + 128) >> 8;
    b[4] = (a0 - a2 - a7 - a3 + 128) >> 8;
    b[5] = (a4 - a6 - s2      + 128) >> 8;
    b[6] = (a4 + a6 - s1      + 128) >> 8;
    b[7] = (a0 + a2 - a1 - a5 + 128) >> 8;
}

static void wmv2_idct_col_mmi(short * b)
{
    int s1, s2;
    int a0, a1, a2, a3, a4, a5, a6, a7;

    /* step 1, with extended precision */
    a0 = (W0 * b[ 0] + W0 * b[32]    ) >> 3;
    a1 = (W1 * b[ 8] + W7 * b[56] + 4) >> 3;
    a2 = (W2 * b[16] + W6 * b[48] + 4) >> 3;
    a3 = (W3 * b[40] - W5 * b[24] + 4) >> 3;
    a4 = (W0 * b[ 0] - W0 * b[32]    ) >> 3;
    a5 = (W5 * b[40] + W3 * b[24] + 4) >> 3;
    a6 = (W6 * b[16] - W2 * b[48] + 4) >> 3;
    a7 = (W7 * b[ 8] - W1 * b[56] + 4) >> 3;

    /* step 2 */
    s1 = (181 * (a1 - a5 + a7 - a3) + 128) >> 8;
    s2 = (181 * (a1 - a5 - a7 + a3) + 128) >> 8;

    /* step 3 */
    b[ 0] = (a0 + a2 + a1 + a5 + 8192) >> 14;
    b[ 8] = (a4 + a6 + s1      + 8192) >> 14;
    b[16] = (a4 - a6 + s2      + 8192) >> 14;
    b[24] = (a0 - a2 + a7 + a3 + 8192) >> 14;

    b[32] = (a0 - a2 - a7 - a3 + 8192) >> 14;
    b[40] = (a4 - a6 - s2      + 8192) >> 14;
    b[48] = (a4 + a6 - s1      + 8192) >> 14;
    b[56] = (a0 + a2 - a1 - a5 + 8192) >> 14;
}

void ff_wmv2_idct_add_mmi(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int i;
    double ftmp[11];

    for (i = 0; i < 64; i += 8)
        wmv2_idct_row_mmi(block + i);
    for (i = 0; i < 8; i++)
        wmv2_idct_col_mmi(block + i);

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],   %[ftmp0]                    \n\t"

        // low 4 loop
        MMI_LDC1(%[ftmp1], %[block], 0x00)
        MMI_LDC1(%[ftmp2], %[block], 0x08)
        MMI_LDC1(%[ftmp3], %[block], 0x10)
        MMI_LDC1(%[ftmp4], %[block], 0x18)
        MMI_LDC1(%[ftmp5], %[block], 0x20)
        MMI_LDC1(%[ftmp6], %[block], 0x28)
        MMI_LDC1(%[ftmp7], %[block], 0x30)
        MMI_LDC1(%[ftmp8], %[block], 0x38)

        MMI_LDC1(%[ftmp9], %[dest], 0x00)
        "punpckhbh  %[ftmp10],  %[ftmp9],   %[ftmp0]                    \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp9],   %[ftmp0]                    \n\t"
        "paddh      %[ftmp1],   %[ftmp1],   %[ftmp9]                    \n\t"
        "paddh      %[ftmp2],   %[ftmp2],   %[ftmp10]                   \n\t"
        "packushb   %[ftmp1],   %[ftmp1],   %[ftmp2]                    \n\t"
        MMI_SDC1(%[ftmp1], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"

        MMI_LDC1(%[ftmp9], %[dest], 0x00)
        "punpckhbh  %[ftmp10],  %[ftmp9],   %[ftmp0]                    \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp9],   %[ftmp0]                    \n\t"
        "paddh      %[ftmp3],   %[ftmp3],   %[ftmp9]                    \n\t"
        "paddh      %[ftmp4],   %[ftmp4],   %[ftmp10]                   \n\t"
        "packushb   %[ftmp3],   %[ftmp3],   %[ftmp4]                    \n\t"
        MMI_SDC1(%[ftmp3], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"

        MMI_LDC1(%[ftmp9], %[dest], 0x00)
        "punpckhbh  %[ftmp10],  %[ftmp9],   %[ftmp0]                    \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp9],   %[ftmp0]                    \n\t"
        "paddh      %[ftmp5],   %[ftmp5],   %[ftmp9]                    \n\t"
        "paddh      %[ftmp6],   %[ftmp6],   %[ftmp10]                   \n\t"
        "packushb   %[ftmp5],   %[ftmp5],   %[ftmp6]                    \n\t"
        MMI_SDC1(%[ftmp5], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"

        MMI_LDC1(%[ftmp9], %[dest], 0x00)
        "punpckhbh  %[ftmp10],  %[ftmp9],   %[ftmp0]                    \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp9],   %[ftmp0]                    \n\t"
        "paddh      %[ftmp7],   %[ftmp7],   %[ftmp9]                    \n\t"
        "paddh      %[ftmp8],   %[ftmp8],   %[ftmp10]                   \n\t"
        "packushb   %[ftmp7],   %[ftmp7],   %[ftmp8]                    \n\t"
        MMI_SDC1(%[ftmp7], %[dest], 0x00)

        PTR_ADDIU  "%[block],   %[block],   0x40                        \n\t"
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"

        // high 4 loop
        MMI_LDC1(%[ftmp1], %[block], 0x00)
        MMI_LDC1(%[ftmp2], %[block], 0x08)
        MMI_LDC1(%[ftmp3], %[block], 0x10)
        MMI_LDC1(%[ftmp4], %[block], 0x18)
        MMI_LDC1(%[ftmp5], %[block], 0x20)
        MMI_LDC1(%[ftmp6], %[block], 0x28)
        MMI_LDC1(%[ftmp7], %[block], 0x30)
        MMI_LDC1(%[ftmp8], %[block], 0x38)

        MMI_LDC1(%[ftmp9], %[dest], 0x00)
        "punpckhbh  %[ftmp10],  %[ftmp9],   %[ftmp0]                    \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp9],   %[ftmp0]                    \n\t"
        "paddh      %[ftmp1],   %[ftmp1],   %[ftmp9]                    \n\t"
        "paddh      %[ftmp2],   %[ftmp2],   %[ftmp10]                   \n\t"
        "packushb   %[ftmp1],   %[ftmp1],   %[ftmp2]                    \n\t"
        MMI_SDC1(%[ftmp1], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"

        MMI_LDC1(%[ftmp9], %[dest], 0x00)
        "punpckhbh  %[ftmp10],  %[ftmp9],   %[ftmp0]                    \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp9],   %[ftmp0]                    \n\t"
        "paddh      %[ftmp3],   %[ftmp3],   %[ftmp9]                    \n\t"
        "paddh      %[ftmp4],   %[ftmp4],   %[ftmp10]                   \n\t"
        "packushb   %[ftmp3],   %[ftmp3],   %[ftmp4]                    \n\t"
        MMI_SDC1(%[ftmp3], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"

        MMI_LDC1(%[ftmp9], %[dest], 0x00)
        "punpckhbh  %[ftmp10],  %[ftmp9],   %[ftmp0]                    \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp9],   %[ftmp0]                    \n\t"
        "paddh      %[ftmp5],   %[ftmp5],   %[ftmp9]                    \n\t"
        "paddh      %[ftmp6],   %[ftmp6],   %[ftmp10]                   \n\t"
        "packushb   %[ftmp5],   %[ftmp5],   %[ftmp6]                    \n\t"
        MMI_SDC1(%[ftmp5], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"

        MMI_LDC1(%[ftmp9], %[dest], 0x00)
        "punpckhbh  %[ftmp10],  %[ftmp9],   %[ftmp0]                    \n\t"
        "punpcklbh  %[ftmp9],   %[ftmp9],   %[ftmp0]                    \n\t"
        "paddh      %[ftmp7],   %[ftmp7],   %[ftmp9]                    \n\t"
        "paddh      %[ftmp8],   %[ftmp8],   %[ftmp10]                   \n\t"
        "packushb   %[ftmp7],   %[ftmp7],   %[ftmp8]                    \n\t"
        MMI_SDC1(%[ftmp7], %[dest], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),
          [block]"+&r"(block),              [dest]"+&r"(dest)
        : [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}

void ff_wmv2_idct_put_mmi(uint8_t *dest, ptrdiff_t line_size, int16_t *block)
{
    int i;
    double ftmp[8];

    for (i = 0; i < 64; i += 8)
        wmv2_idct_row_mmi(block + i);
    for (i = 0; i < 8; i++)
        wmv2_idct_col_mmi(block + i);

    __asm__ volatile (
        // low 4 loop
        MMI_LDC1(%[ftmp0], %[block], 0x00)
        MMI_LDC1(%[ftmp1], %[block], 0x08)
        MMI_LDC1(%[ftmp2], %[block], 0x10)
        MMI_LDC1(%[ftmp3], %[block], 0x18)
        MMI_LDC1(%[ftmp4], %[block], 0x20)
        MMI_LDC1(%[ftmp5], %[block], 0x28)
        MMI_LDC1(%[ftmp6], %[block], 0x30)
        MMI_LDC1(%[ftmp7], %[block], 0x38)
        "packushb   %[ftmp0],   %[ftmp0],   %[ftmp1]                    \n\t"
        "packushb   %[ftmp2],   %[ftmp2],   %[ftmp3]                    \n\t"
        "packushb   %[ftmp4],   %[ftmp4],   %[ftmp5]                    \n\t"
        "packushb   %[ftmp6],   %[ftmp6],   %[ftmp7]                    \n\t"
        MMI_SDC1(%[ftmp0], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"
        MMI_SDC1(%[ftmp2], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"
        MMI_SDC1(%[ftmp4], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"
        MMI_SDC1(%[ftmp6], %[dest], 0x00)

        PTR_ADDIU  "%[block],   %[block],   0x40                        \n\t"
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"

        // high 4 loop
        MMI_LDC1(%[ftmp0], %[block], 0x00)
        MMI_LDC1(%[ftmp1], %[block], 0x08)
        MMI_LDC1(%[ftmp2], %[block], 0x10)
        MMI_LDC1(%[ftmp3], %[block], 0x18)
        MMI_LDC1(%[ftmp4], %[block], 0x20)
        MMI_LDC1(%[ftmp5], %[block], 0x28)
        MMI_LDC1(%[ftmp6], %[block], 0x30)
        MMI_LDC1(%[ftmp7], %[block], 0x38)
        "packushb   %[ftmp0],   %[ftmp0],   %[ftmp1]                    \n\t"
        "packushb   %[ftmp2],   %[ftmp2],   %[ftmp3]                    \n\t"
        "packushb   %[ftmp4],   %[ftmp4],   %[ftmp5]                    \n\t"
        "packushb   %[ftmp6],   %[ftmp6],   %[ftmp7]                    \n\t"
        MMI_SDC1(%[ftmp0], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"
        MMI_SDC1(%[ftmp2], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"
        MMI_SDC1(%[ftmp4], %[dest], 0x00)
        PTR_ADDU   "%[dest],    %[dest],    %[line_size]                \n\t"
        MMI_SDC1(%[ftmp6], %[dest], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [block]"+&r"(block),              [dest]"+&r"(dest)
        : [line_size]"r"((mips_reg)line_size)
        : "memory"
    );
}
