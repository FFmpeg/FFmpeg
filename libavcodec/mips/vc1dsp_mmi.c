/*
 * VC-1 and WMV3 - DSP functions Loongson MMI-optimized
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
#include "libavcodec/vc1dsp.h"
#include "constants.h"
#include "vc1dsp_mips.h"
#include "hpeldsp_mips.h"
#include "libavutil/mips/mmiutils.h"


#define VC1_INV_TRANCS_8_STEP1_MMI(fp1,   fp2,   fp3,   fp4,                \
                                   o1,    o2,    o3,    o4,                 \
                                   t1,    t2,    t3,    t4,                 \
                                   ff_p1, ff_p2, ff_p3, ff_p4)              \
        "pmullh     "#t1"   ,   "#fp1"  ,   "#ff_p1"                \n\t"   \
        "pmullh     "#t2"   ,   "#fp2"  ,   "#ff_p2"                \n\t"   \
        "pmullh     "#t3"   ,   "#fp3"  ,   "#ff_p3"                \n\t"   \
        "pmullh     "#t4"   ,   "#fp4"  ,   "#ff_p4"                \n\t"   \
        "paddh      "#o1"   ,   "#t1"   ,   "#t2"                   \n\t"   \
        "paddh      "#o1"   ,   "#o1"   ,   "#t3"                   \n\t"   \
        "paddh      "#o1"   ,   "#o1"   ,   "#t4"                   \n\t"   \
                                                                            \
        "pmullh     "#t1"   ,   "#fp1"  ,   "#ff_p2"                \n\t"   \
        "pmullh     "#t2"   ,   "#fp2"  ,   "#ff_p4"                \n\t"   \
        "pmullh     "#t3"   ,   "#fp3"  ,   "#ff_p1"                \n\t"   \
        "pmullh     "#t4"   ,   "#fp4"  ,   "#ff_p3"                \n\t"   \
        "psubh      "#o2"   ,   "#t1"   ,   "#t2"                   \n\t"   \
        "psubh      "#o2"   ,   "#o2"   ,   "#t3"                   \n\t"   \
        "psubh      "#o2"   ,   "#o2"   ,   "#t4"                   \n\t"   \
                                                                            \
        "pmullh     "#t1"   ,   "#fp1"  ,   "#ff_p3"                \n\t"   \
        "pmullh     "#t2"   ,   "#fp2"  ,   "#ff_p1"                \n\t"   \
        "pmullh     "#t3"   ,   "#fp3"  ,   "#ff_p4"                \n\t"   \
        "pmullh     "#t4"   ,   "#fp4"  ,   "#ff_p2"                \n\t"   \
        "psubh      "#o3"   ,   "#t1"   ,   "#t2"                   \n\t"   \
        "paddh      "#o3"   ,   "#o3"   ,   "#t3"                   \n\t"   \
        "paddh      "#o3"   ,   "#o3"   ,   "#t4"                   \n\t"   \
                                                                            \
        "pmullh     "#t1"   ,   "#fp1"  ,   "#ff_p4"                \n\t"   \
        "pmullh     "#t2"   ,   "#fp2"  ,   "#ff_p3"                \n\t"   \
        "pmullh     "#t3"   ,   "#fp3"  ,   "#ff_p2"                \n\t"   \
        "pmullh     "#t4"   ,   "#fp4"  ,   "#ff_p1"                \n\t"   \
        "psubh      "#o4"   ,   "#t1"   ,   "#t2"                   \n\t"   \
        "paddh      "#o4"   ,   "#o4"   ,   "#t3"                   \n\t"   \
        "psubh      "#o4"   ,   "#o4"   ,   "#t4"                   \n\t"


#define VC1_INV_TRANCS_8_STEP2_MMI(fp1,   fp2,   fp3,   fp4,                \
                                   fp5,   fp6,   fp7,   fp8,                \
                                   o1,    o2,    o3,    o4,                 \
                                   ff_p1, ff_p2, ff_p3, ff_pw)              \
        "paddh      "#fp5"  ,   "#fp1"  ,   "#fp2"                  \n\t"   \
        "psubh      "#fp6"  ,   "#fp1"  ,   "#fp2"                  \n\t"   \
        "pmullh     "#fp5"  ,   "#fp5"  ,   "#ff_p1"                \n\t"   \
        "pmullh     "#fp6"  ,   "#fp6"  ,   "#ff_p1"                \n\t"   \
        "paddh      "#fp5"  ,   "#fp5"  ,   "#ff_pw"                \n\t"   \
        "paddh      "#fp6"  ,   "#fp6"  ,   "#ff_pw"                \n\t"   \
                                                                            \
        "pmullh     "#fp1"  ,   "#fp3"  ,   "#ff_p2"                \n\t"   \
        "pmullh     "#fp2"  ,   "#fp4"  ,   "#ff_p3"                \n\t"   \
        "pmullh     "#fp3"  ,   "#fp3"  ,   "#ff_p3"                \n\t"   \
        "pmullh     "#fp4"  ,   "#fp4"  ,   "#ff_p2"                \n\t"   \
        "paddh      "#fp7"  ,   "#fp1"  ,   "#fp2"                  \n\t"   \
        "psubh      "#fp8"  ,   "#fp3"  ,   "#fp4"                  \n\t"   \
                                                                            \
        "paddh      "#fp1"  ,   "#fp5"  ,   "#fp7"                  \n\t"   \
        "paddh      "#fp2"  ,   "#fp6"  ,   "#fp8"                  \n\t"   \
        "psubh      "#fp3"  ,   "#fp6"  ,   "#fp8"                  \n\t"   \
        "psubh      "#fp4"  ,   "#fp5"  ,   "#fp7"                  \n\t"   \
                                                                            \
        "paddh      "#fp5"  ,   "#fp1"  ,   "#o1"                   \n\t"   \
        "paddh      "#fp6"  ,   "#fp2"  ,   "#o2"                   \n\t"   \
        "paddh      "#fp7"  ,   "#fp3"  ,   "#o3"                   \n\t"   \
        "paddh      "#fp8"  ,   "#fp4"  ,   "#o4"                   \n\t"   \
                                                                            \
        "psubh      "#fp4"  ,   "#fp4"  ,   "#o4"                   \n\t"   \
        "psubh      "#fp3"  ,   "#fp3"  ,   "#o3"                   \n\t"   \
        "psubh      "#fp2"  ,   "#fp2"  ,   "#o2"                   \n\t"   \
        "psubh      "#fp1"  ,   "#fp1"  ,   "#o1"                   \n\t"


#define VC1_INV_TRANCS_4_STEP1_MMI(fp1,   fp2,   fp3,   fp4,                \
                                   fp5,   fp6,   fp7,   fp8,                \
                                   ff_p1, ff_p2, ff_p3, ff_pw)              \
        "paddh      "#fp5"  ,   "#fp1"  ,   "#fp2"                  \n\t"   \
        "psubh      "#fp6"  ,   "#fp1"  ,   "#fp2"                  \n\t"   \
        "pmullh     "#fp5"  ,   "#fp5"  ,   "#ff_p1"                \n\t"   \
        "pmullh     "#fp6"  ,   "#fp6"  ,   "#ff_p1"                \n\t"   \
        "paddh      "#fp5"  ,   "#fp5"  ,   "#ff_pw"                \n\t"   \
        "paddh      "#fp6"  ,   "#fp6"  ,   "#ff_pw"                \n\t"   \
                                                                            \
        "pmullh     "#fp1"  ,   "#fp3"  ,   "#ff_p2"                \n\t"   \
        "pmullh     "#fp2"  ,   "#fp4"  ,   "#ff_p3"                \n\t"   \
        "pmullh     "#fp3"  ,   "#fp3"  ,   "#ff_p3"                \n\t"   \
        "pmullh     "#fp4"  ,   "#fp4"  ,   "#ff_p2"                \n\t"   \
        "paddh      "#fp7"  ,   "#fp1"  ,   "#fp2"                  \n\t"   \
        "psubh      "#fp8"  ,   "#fp3"  ,   "#fp4"                  \n\t"   \
                                                                            \
        "paddh      "#fp1"  ,   "#fp5"  ,   "#fp7"                  \n\t"   \
        "psubh      "#fp2"  ,   "#fp6"  ,   "#fp8"                  \n\t"   \
        "paddh      "#fp3"  ,   "#fp6"  ,   "#fp8"                  \n\t"   \
        "psubh      "#fp4"  ,   "#fp5"  ,   "#fp7"                  \n\t"


#define VC1_INV_TRANCS_4_STEP2_MMI(fp1, fp2, fp3, fp4,                      \
                                   fp5, fp6, fp7, fp8, zero)                \
        "punpcklbh  "#fp5"  ,   "#fp5"  ,   "#zero"                 \n\t"   \
        "punpcklbh  "#fp6"  ,   "#fp6"  ,   "#zero"                 \n\t"   \
        "punpcklbh  "#fp7"  ,   "#fp7"  ,   "#zero"                 \n\t"   \
        "punpcklbh  "#fp8"  ,   "#fp8"  ,   "#zero"                 \n\t"   \
                                                                            \
        "paddh      "#fp1"  ,   "#fp1"  ,   "#fp5"                  \n\t"   \
        "paddh      "#fp2"  ,   "#fp2"  ,   "#fp6"                  \n\t"   \
        "paddh      "#fp3"  ,   "#fp3"  ,   "#fp7"                  \n\t"   \
        "paddh      "#fp4"  ,   "#fp4"  ,   "#fp8"                  \n\t"   \
                                                                            \
        "packushb   "#fp1"  ,   "#fp1"  ,   "#zero"                 \n\t"   \
        "packushb   "#fp2"  ,   "#fp2"  ,   "#zero"                 \n\t"   \
        "packushb   "#fp3"  ,   "#fp3"  ,   "#zero"                 \n\t"   \
        "packushb   "#fp4"  ,   "#fp4"  ,   "#zero"                 \n\t"


/* Do inverse transform on 8x8 block */
void ff_vc1_inv_trans_8x8_dc_mmi(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
{
    int dc = block[0];
    double ftmp[9];
    mips_reg addr[1];
    int count;

    dc = (3 * dc +  1) >> 1;
    dc = (3 * dc + 16) >> 5;

    __asm__ volatile(
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "pshufh     %[dc],      %[dc],          %[ftmp0]                \n\t"
        "li         %[count],   0x02                                    \n\t"

        "1:                                                             \n\t"
        MMI_LDC1(%[ftmp1], %[dest], 0x00)
        PTR_ADDU   "%[addr0],   %[dest],        %[linesize]             \n\t"
        MMI_LDC1(%[ftmp2], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[linesize]             \n\t"
        MMI_LDC1(%[ftmp3], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[linesize]             \n\t"
        MMI_LDC1(%[ftmp4], %[addr0], 0x00)

        "punpckhbh  %[ftmp5],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"

        "paddsh     %[ftmp1],   %[ftmp1],       %[dc]                   \n\t"
        "paddsh     %[ftmp2],   %[ftmp2],       %[dc]                   \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[dc]                   \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[dc]                   \n\t"
        "paddsh     %[ftmp5],   %[ftmp5],       %[dc]                   \n\t"
        "paddsh     %[ftmp6],   %[ftmp6],       %[dc]                   \n\t"
        "paddsh     %[ftmp7],   %[ftmp7],       %[dc]                   \n\t"
        "paddsh     %[ftmp8],   %[ftmp8],       %[dc]                   \n\t"

        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "packushb   %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp8]                \n\t"

        MMI_SDC1(%[ftmp1], %[dest], 0x00)
        PTR_ADDU   "%[addr0],   %[dest],        %[linesize]             \n\t"
        MMI_SDC1(%[ftmp2], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[linesize]             \n\t"
        MMI_SDC1(%[ftmp3], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],       %[linesize]             \n\t"
        MMI_SDC1(%[ftmp4], %[addr0], 0x00)

        "addiu      %[count],   %[count],       -0x01                   \n\t"
        PTR_ADDU   "%[dest],    %[addr0],       %[linesize]             \n\t"
        "bnez       %[count],   1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [addr0]"=&r"(addr[0]),
          [count]"=&r"(count),          [dest]"+&r"(dest)
        : [linesize]"r"((mips_reg)linesize),
          [dc]"f"(dc)
        : "memory"
    );
}

#if _MIPS_SIM != _ABIO32
void ff_vc1_inv_trans_8x8_mmi(int16_t block[64])
{
    DECLARE_ALIGNED(16, int16_t, temp[64]);
    int16_t *src = block;
    int16_t *dst = temp;
    double ftmp[16];
    uint32_t count, tmp[1];

    // 1st loop
    __asm__ volatile (
        "li         %[tmp0],    0x03                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp0]                                \n\t"
        "li         %[count],   0x02                                    \n\t"

        "1:                                                             \n\t"
        MMI_LDC1(%[ftmp5], %[src], 0x10)
        MMI_LDC1(%[ftmp6], %[src], 0x30)
        MMI_LDC1(%[ftmp7], %[src], 0x50)
        MMI_LDC1(%[ftmp8], %[src], 0x70)

        VC1_INV_TRANCS_8_STEP1_MMI(%[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                                   %[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ff_pw_16], %[ff_pw_15], %[ff_pw_9],
                                   %[ff_pw_4])

        MMI_LDC1(%[ftmp1], %[src], 0x00)
        MMI_LDC1(%[ftmp2], %[src], 0x40)
        MMI_LDC1(%[ftmp3], %[src], 0x20)
        MMI_LDC1(%[ftmp4], %[src], 0x60)

        VC1_INV_TRANCS_8_STEP2_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                                   %[ff_pw_12], %[ff_pw_16], %[ff_pw_6],
                                   %[ff_pw_4])


        PSRAH_8_MMI(%[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                    %[ftmp4], %[ftmp3], %[ftmp2], %[ftmp1], %[ftmp0])

        TRANSPOSE_4H(%[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                     %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                     %[ftmp13], %[tmp0],  %[ftmp14], %[ftmp15])

        MMI_SDC1(%[ftmp5], %[dst], 0x00)
        MMI_SDC1(%[ftmp6], %[dst], 0x10)
        MMI_SDC1(%[ftmp7], %[dst], 0x20)
        MMI_SDC1(%[ftmp8], %[dst], 0x30)

        TRANSPOSE_4H(%[ftmp4], %[ftmp3], %[ftmp2], %[ftmp1],
                     %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                     %[ftmp13], %[tmp0],  %[ftmp14], %[ftmp15])

        MMI_SDC1(%[ftmp4], %[dst], 0x08)
        MMI_SDC1(%[ftmp3], %[dst], 0x18)
        MMI_SDC1(%[ftmp2], %[dst], 0x28)
        MMI_SDC1(%[ftmp1], %[dst], 0x38)

        "addiu      %[count],   %[count],  -0x01                        \n\t"
        PTR_ADDIU  "%[src],     %[src],     0x08                        \n\t"
        PTR_ADDIU  "%[dst],     %[dst],     0x40                        \n\t"
        "bnez       %[count],   1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),      [ftmp11]"=&f"(ftmp[11]),
          [ftmp12]"=&f"(ftmp[12]),      [ftmp13]"=&f"(ftmp[13]),
          [ftmp14]"=&f"(ftmp[14]),      [ftmp15]"=&f"(ftmp[15]),
          [tmp0]"=&r"(tmp[0]),
          [count]"=&r"(count),
          [src]"+&r"(src),              [dst]"+&r"(dst)
        : [ff_pw_4]"f"(ff_pw_4),        [ff_pw_6]"f"(ff_pw_6),
          [ff_pw_9]"f"(ff_pw_9),        [ff_pw_12]"f"(ff_pw_12),
          [ff_pw_15]"f"(ff_pw_15),      [ff_pw_16]"f"(ff_pw_16)
        : "memory"
    );

    src = temp;
    dst = block;

    // 2nd loop
    __asm__ volatile (
        "li         %[tmp0],    0x07                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp0]                                \n\t"
        "li         %[count],   0x02                                    \n\t"

        "1:                                                             \n\t"
        MMI_LDC1(%[ftmp5], %[src], 0x10)
        MMI_LDC1(%[ftmp6], %[src], 0x30)
        MMI_LDC1(%[ftmp7], %[src], 0x50)
        MMI_LDC1(%[ftmp8], %[src], 0x70)

        VC1_INV_TRANCS_8_STEP1_MMI(%[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                                   %[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ff_pw_16], %[ff_pw_15], %[ff_pw_9],
                                   %[ff_pw_4])

        MMI_LDC1(%[ftmp1], %[src], 0x00)
        MMI_LDC1(%[ftmp2], %[src], 0x40)
        MMI_LDC1(%[ftmp3], %[src], 0x20)
        MMI_LDC1(%[ftmp4], %[src], 0x60)

        VC1_INV_TRANCS_8_STEP2_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                                   %[ff_pw_12], %[ff_pw_16], %[ff_pw_6],
                                   %[ff_pw_64])

        "paddh      %[ftmp4],   %[ftmp4],   %[ff_pw_1]                  \n\t"
        "paddh      %[ftmp3],   %[ftmp3],   %[ff_pw_1]                  \n\t"
        "paddh      %[ftmp2],   %[ftmp2],   %[ff_pw_1]                  \n\t"
        "paddh      %[ftmp1],   %[ftmp1],   %[ff_pw_1]                  \n\t"

        PSRAH_8_MMI(%[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                    %[ftmp4], %[ftmp3], %[ftmp2], %[ftmp1], %[ftmp0])

        MMI_SDC1(%[ftmp5], %[dst], 0x00)
        MMI_SDC1(%[ftmp6], %[dst], 0x10)
        MMI_SDC1(%[ftmp7], %[dst], 0x20)
        MMI_SDC1(%[ftmp8], %[dst], 0x30)

        MMI_SDC1(%[ftmp4], %[dst], 0x40)
        MMI_SDC1(%[ftmp3], %[dst], 0x50)
        MMI_SDC1(%[ftmp2], %[dst], 0x60)
        MMI_SDC1(%[ftmp1], %[dst], 0x70)

        "addiu      %[count],   %[count],  -0x01                        \n\t"
        PTR_ADDIU  "%[src],     %[src],     0x08                        \n\t"
        PTR_ADDIU  "%[dst],     %[dst],     0x08                        \n\t"
        "bnez       %[count],   1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),      [ftmp11]"=&f"(ftmp[11]),
          [ftmp12]"=&f"(ftmp[12]),      [ftmp13]"=&f"(ftmp[13]),
          [ftmp14]"=&f"(ftmp[14]),      [ftmp15]"=&f"(ftmp[15]),
          [tmp0]"=&r"(tmp[0]),
          [count]"=&r"(count),
          [src]"+&r"(src),              [dst]"+&r"(dst)
        : [ff_pw_1]"f"(ff_pw_1),        [ff_pw_4]"f"(ff_pw_4),
          [ff_pw_6]"f"(ff_pw_6),        [ff_pw_9]"f"(ff_pw_9),
          [ff_pw_12]"f"(ff_pw_12),      [ff_pw_15]"f"(ff_pw_15),
          [ff_pw_16]"f"(ff_pw_16),      [ff_pw_64]"f"(ff_pw_64)
        : "memory"
    );
}
#endif

/* Do inverse transform on 8x4 part of block */
void ff_vc1_inv_trans_8x4_dc_mmi(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
{
    int dc = block[0];
    double ftmp[9];

    dc = ( 3 * dc +  1) >> 1;
    dc = (17 * dc + 64) >> 7;

    __asm__ volatile(
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "pshufh     %[dc],      %[dc],          %[ftmp0]                \n\t"

        MMI_LDC1(%[ftmp1], %[dest0], 0x00)
        MMI_LDC1(%[ftmp2], %[dest1], 0x00)
        MMI_LDC1(%[ftmp3], %[dest2], 0x00)
        MMI_LDC1(%[ftmp4], %[dest3], 0x00)

        "punpckhbh  %[ftmp5],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpckhbh  %[ftmp8],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"

        "paddsh     %[ftmp1],   %[ftmp1],       %[dc]                   \n\t"
        "paddsh     %[ftmp2],   %[ftmp2],       %[dc]                   \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[dc]                   \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[dc]                   \n\t"
        "paddsh     %[ftmp5],   %[ftmp5],       %[dc]                   \n\t"
        "paddsh     %[ftmp6],   %[ftmp6],       %[dc]                   \n\t"
        "paddsh     %[ftmp7],   %[ftmp7],       %[dc]                   \n\t"
        "paddsh     %[ftmp8],   %[ftmp8],       %[dc]                   \n\t"

        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp5]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp6]                \n\t"
        "packushb   %[ftmp3],   %[ftmp3],       %[ftmp7]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp8]                \n\t"

        MMI_SDC1(%[ftmp1], %[dest0], 0x00)
        MMI_SDC1(%[ftmp2], %[dest1], 0x00)
        MMI_SDC1(%[ftmp3], %[dest2], 0x00)
        MMI_SDC1(%[ftmp4], %[dest3], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8])
        : [dest0]"r"(dest+0*linesize),  [dest1]"r"(dest+1*linesize),
          [dest2]"r"(dest+2*linesize),  [dest3]"r"(dest+3*linesize),
          [dc]"f"(dc)
        : "memory"
    );
}

#if _MIPS_SIM != _ABIO32
void ff_vc1_inv_trans_8x4_mmi(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
{
    int16_t *src = block;
    int16_t *dst = block;
    double ftmp[16];
    uint32_t tmp[1];
    mips_reg addr[1];
    DECLARE_VAR_LOW32;

    // 1st loop
    __asm__ volatile (
        MMI_LDC1(%[ftmp1], %[src], 0x00)
        MMI_LDC1(%[ftmp2], %[src], 0x08)
        MMI_LDC1(%[ftmp3], %[src], 0x10)
        MMI_LDC1(%[ftmp4], %[src], 0x18)
        MMI_LDC1(%[ftmp5], %[src], 0x20)
        MMI_LDC1(%[ftmp6], %[src], 0x28)
        MMI_LDC1(%[ftmp7], %[src], 0x30)
        MMI_LDC1(%[ftmp8], %[src], 0x38)

        //             a1        b1        a3        b2
        TRANSPOSE_4H(%[ftmp1], %[ftmp3], %[ftmp5], %[ftmp7],
                     %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                     %[ftmp13], %[tmp0],  %[ftmp14], %[ftmp15])

        //             a2        b3        a4        b4
        TRANSPOSE_4H(%[ftmp2], %[ftmp4], %[ftmp6], %[ftmp8],
                     %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                     %[ftmp13], %[tmp0],  %[ftmp14], %[ftmp15])

        // input b1 b2 b3 b4
        VC1_INV_TRANCS_8_STEP1_MMI(%[ftmp3], %[ftmp7], %[ftmp4], %[ftmp8],
                                   %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                                   %[ftmp0], %[ftmp13], %[ftmp14], %[ftmp15],
                                   %[ff_pw_16], %[ff_pw_15], %[ff_pw_9],
                                   %[ff_pw_4])
        // input a1 a2 a3 a4
        VC1_INV_TRANCS_8_STEP2_MMI(%[ftmp1], %[ftmp2], %[ftmp5], %[ftmp6],
                                   %[ftmp3], %[ftmp7], %[ftmp4], %[ftmp8],
                                   %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                                   %[ff_pw_12], %[ff_pw_16], %[ff_pw_6],
                                   %[ff_pw_4])

        "li         %[tmp0],    0x03                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp0]                                \n\t"

        PSRAH_8_MMI(%[ftmp3], %[ftmp7], %[ftmp4], %[ftmp8],
                    %[ftmp6], %[ftmp5], %[ftmp2], %[ftmp1], %[ftmp0])

        TRANSPOSE_4H(%[ftmp3], %[ftmp7], %[ftmp4], %[ftmp8],
                     %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                     %[ftmp13], %[tmp0],  %[ftmp14], %[ftmp15])

        MMI_SDC1(%[ftmp3], %[dst], 0x00)
        MMI_SDC1(%[ftmp7], %[dst], 0x10)
        MMI_SDC1(%[ftmp4], %[dst], 0x20)
        MMI_SDC1(%[ftmp8], %[dst], 0x30)

        TRANSPOSE_4H(%[ftmp6], %[ftmp5], %[ftmp2], %[ftmp1],
                     %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                     %[ftmp13], %[tmp0],  %[ftmp14], %[ftmp15])

        MMI_SDC1(%[ftmp6], %[dst], 0x08)
        MMI_SDC1(%[ftmp5], %[dst], 0x18)
        MMI_SDC1(%[ftmp2], %[dst], 0x28)
        MMI_SDC1(%[ftmp1], %[dst], 0x38)
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),      [ftmp11]"=&f"(ftmp[11]),
          [ftmp12]"=&f"(ftmp[12]),      [ftmp13]"=&f"(ftmp[13]),
          [ftmp14]"=&f"(ftmp[14]),      [ftmp15]"=&f"(ftmp[15]),
          [tmp0]"=&r"(tmp[0])
        : [src]"r"(src),                [dst]"r"(dst),
          [ff_pw_4]"f"(ff_pw_4),        [ff_pw_6]"f"(ff_pw_6),
          [ff_pw_9]"f"(ff_pw_9),        [ff_pw_12]"f"(ff_pw_12),
          [ff_pw_15]"f"(ff_pw_15),      [ff_pw_16]"f"(ff_pw_16)
        : "memory"
    );

    src = block;

    // 2nd loop
    __asm__ volatile (
        "li         %[tmp0],    0x07                                    \n\t"
        "xor        %[ftmp0],   %[ftmp0],   %[ftmp0]                    \n\t"
        "mtc1       %[tmp0],    %[ftmp9]                                \n\t"

        // dest low 32bit
        MMI_LDC1(%[ftmp1], %[src], 0x00)
        MMI_LDC1(%[ftmp2], %[src], 0x20)
        MMI_LDC1(%[ftmp3], %[src], 0x30)
        MMI_LDC1(%[ftmp4], %[src], 0x10)

        VC1_INV_TRANCS_4_STEP1_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ff_pw_17], %[ff_pw_10], %[ff_pw_22],
                                   %[ff_pw_64])

        PSRAH_4_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4], %[ftmp9])

        MMI_LWC1(%[ftmp5], %[dest], 0x00)
        PTR_ADDU   "%[addr0],   %[dest],    %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp6], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp7], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp8], %[addr0], 0x00)

        VC1_INV_TRANCS_4_STEP2_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ftmp0])

        MMI_SWC1(%[ftmp1], %[dest], 0x00)
        PTR_ADDU   "%[addr0],   %[dest],    %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp2], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp3], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp4], %[addr0], 0x00)

        // dest high 32bit
        MMI_LDC1(%[ftmp1], %[src], 0x08)
        MMI_LDC1(%[ftmp2], %[src], 0x28)
        MMI_LDC1(%[ftmp3], %[src], 0x38)
        MMI_LDC1(%[ftmp4], %[src], 0x18)

        VC1_INV_TRANCS_4_STEP1_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ff_pw_17], %[ff_pw_10], %[ff_pw_22],
                                   %[ff_pw_64])

        PSRAH_4_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4], %[ftmp9])

        MMI_LWC1(%[ftmp5], %[dest], 0x04)
        PTR_ADDU   "%[addr0],   %[dest],    %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp6], %[addr0], 0x04)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp7], %[addr0], 0x04)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp8], %[addr0], 0x04)

        VC1_INV_TRANCS_4_STEP2_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ftmp0])

        MMI_SWC1(%[ftmp1], %[dest], 0x04)
        PTR_ADDU   "%[addr0],   %[dest],    %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp2], %[addr0], 0x04)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp3], %[addr0], 0x04)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp4], %[addr0], 0x04)

        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          [addr0]"=&r"(addr[0])
        : [src]"r"(src),                [dest]"r"(dest),
          [linesize]"r"((mips_reg)linesize),
          [ff_pw_17]"f"(ff_pw_17),      [ff_pw_22]"f"(ff_pw_22),
          [ff_pw_10]"f"(ff_pw_10),      [ff_pw_64]"f"(ff_pw_64)
        : "memory"
    );
}
#endif

/* Do inverse transform on 4x8 parts of block */
void ff_vc1_inv_trans_4x8_dc_mmi(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
{
    int dc = block[0];
    double ftmp[9];
    DECLARE_VAR_LOW32;

    dc = (17 * dc +  4) >> 3;
    dc = (12 * dc + 64) >> 7;

    __asm__ volatile(
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "pshufh     %[dc],      %[dc],          %[ftmp0]                \n\t"

        MMI_LWC1(%[ftmp1], %[dest0], 0x00)
        MMI_LWC1(%[ftmp2], %[dest1], 0x00)
        MMI_LWC1(%[ftmp3], %[dest2], 0x00)
        MMI_LWC1(%[ftmp4], %[dest3], 0x00)
        MMI_LWC1(%[ftmp5], %[dest4], 0x00)
        MMI_LWC1(%[ftmp6], %[dest5], 0x00)
        MMI_LWC1(%[ftmp7], %[dest6], 0x00)
        MMI_LWC1(%[ftmp8], %[dest7], 0x00)

        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp7],   %[ftmp7],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp8],   %[ftmp8],       %[ftmp0]                \n\t"

        "paddsh     %[ftmp1],   %[ftmp1],       %[dc]                   \n\t"
        "paddsh     %[ftmp2],   %[ftmp2],       %[dc]                   \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[dc]                   \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[dc]                   \n\t"
        "paddsh     %[ftmp5],   %[ftmp5],       %[dc]                   \n\t"
        "paddsh     %[ftmp6],   %[ftmp6],       %[dc]                   \n\t"
        "paddsh     %[ftmp7],   %[ftmp7],       %[dc]                   \n\t"
        "paddsh     %[ftmp8],   %[ftmp8],       %[dc]                   \n\t"

        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "packushb   %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"
        "packushb   %[ftmp5],   %[ftmp5],       %[ftmp0]                \n\t"
        "packushb   %[ftmp6],   %[ftmp6],       %[ftmp0]                \n\t"
        "packushb   %[ftmp7],   %[ftmp7],       %[ftmp0]                \n\t"
        "packushb   %[ftmp8],   %[ftmp8],       %[ftmp0]                \n\t"

        MMI_SWC1(%[ftmp1], %[dest0], 0x00)
        MMI_SWC1(%[ftmp2], %[dest1], 0x00)
        MMI_SWC1(%[ftmp3], %[dest2], 0x00)
        MMI_SWC1(%[ftmp4], %[dest3], 0x00)
        MMI_SWC1(%[ftmp5], %[dest4], 0x00)
        MMI_SWC1(%[ftmp6], %[dest5], 0x00)
        MMI_SWC1(%[ftmp7], %[dest6], 0x00)
        MMI_SWC1(%[ftmp8], %[dest7], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          RESTRICT_ASM_LOW32
          [ftmp8]"=&f"(ftmp[8])
        : [dest0]"r"(dest+0*linesize),  [dest1]"r"(dest+1*linesize),
          [dest2]"r"(dest+2*linesize),  [dest3]"r"(dest+3*linesize),
          [dest4]"r"(dest+4*linesize),  [dest5]"r"(dest+5*linesize),
          [dest6]"r"(dest+6*linesize),  [dest7]"r"(dest+7*linesize),
          [dc]"f"(dc)
        : "memory"
    );
}

#if _MIPS_SIM != _ABIO32
void ff_vc1_inv_trans_4x8_mmi(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
{
    int16_t *src = block;
    int16_t *dst = block;
    double ftmp[16];
    uint32_t count, tmp[1];
    mips_reg addr[1];
    DECLARE_VAR_LOW32;

    // 1st loop
    __asm__ volatile (
        "li         %[count],   0x02                                    \n\t"
        "li         %[tmp0],    0x03                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp0]                                \n\t"

        "1:                                                             \n\t"
        MMI_LDC1(%[ftmp1], %[src], 0x00)
        MMI_LDC1(%[ftmp2], %[src], 0x10)
        MMI_LDC1(%[ftmp3], %[src], 0x20)
        MMI_LDC1(%[ftmp4], %[src], 0x30)

        TRANSPOSE_4H(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                     %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                     %[ftmp9], %[tmp0],  %[ftmp10], %[ftmp11])

        //                              t1        t2        t3        t4
        VC1_INV_TRANCS_4_STEP1_MMI(%[ftmp1], %[ftmp3], %[ftmp4], %[ftmp2],
                                   %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ff_pw_17], %[ff_pw_10], %[ff_pw_22],
                                   %[ff_pw_4])

        PSRAH_4_MMI(%[ftmp1], %[ftmp3], %[ftmp4], %[ftmp2], %[ftmp0])

        TRANSPOSE_4H(%[ftmp1], %[ftmp3], %[ftmp4], %[ftmp2],
                     %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                     %[ftmp9], %[tmp0],  %[ftmp10], %[ftmp11])

        MMI_SDC1(%[ftmp1], %[dst], 0x00)
        MMI_SDC1(%[ftmp3], %[dst], 0x10)
        MMI_SDC1(%[ftmp4], %[dst], 0x20)
        MMI_SDC1(%[ftmp2], %[dst], 0x30)

        "addiu      %[count],   %[count],  -0x01                        \n\t"
        PTR_ADDIU  "%[src],     %[src],     0x40                        \n\t"
        PTR_ADDIU  "%[dst],     %[dst],     0x40                        \n\t"
        "bnez       %[count],   1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),      [ftmp11]"=&f"(ftmp[11]),
          [tmp0]"=&r"(tmp[0]),
          [count]"=&r"(count),
          [src]"+&r"(src),              [dst]"+&r"(dst)
        : [ff_pw_17]"f"(ff_pw_17),      [ff_pw_10]"f"(ff_pw_10),
          [ff_pw_22]"f"(ff_pw_22),      [ff_pw_4]"f"(ff_pw_4)
        : "memory"
    );

    src = block;

    // 2nd loop
    __asm__ volatile (
        "li         %[tmp0],    0x07                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp0]                                \n\t"

        MMI_LDC1(%[ftmp5], %[src], 0x10)
        MMI_LDC1(%[ftmp6], %[src], 0x30)
        MMI_LDC1(%[ftmp7], %[src], 0x50)
        MMI_LDC1(%[ftmp8], %[src], 0x70)

        VC1_INV_TRANCS_8_STEP1_MMI(%[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                                   %[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ff_pw_16], %[ff_pw_15], %[ff_pw_9],
                                   %[ff_pw_4])

        MMI_LDC1(%[ftmp1], %[src], 0x00)
        MMI_LDC1(%[ftmp2], %[src], 0x40)
        MMI_LDC1(%[ftmp3], %[src], 0x20)
        MMI_LDC1(%[ftmp4], %[src], 0x60)

        VC1_INV_TRANCS_8_STEP2_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                                   %[ff_pw_12], %[ff_pw_16], %[ff_pw_6],
                                   %[ff_pw_64])

        "paddh      %[ftmp4],   %[ftmp4],   %[ff_pw_1]                  \n\t"
        "paddh      %[ftmp3],   %[ftmp3],   %[ff_pw_1]                  \n\t"
        "paddh      %[ftmp2],   %[ftmp2],   %[ff_pw_1]                  \n\t"
        "paddh      %[ftmp1],   %[ftmp1],   %[ff_pw_1]                  \n\t"

        PSRAH_8_MMI(%[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                    %[ftmp4], %[ftmp3], %[ftmp2], %[ftmp1], %[ftmp0])

        "xor        %[ftmp0],   %[ftmp0],   %[ftmp0]                    \n\t"

        // dest low
        MMI_LWC1(%[ftmp9], %[dest], 0x00)
        PTR_ADDU   "%[addr0],   %[dest],    %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp10], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp11], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp12], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"

        VC1_INV_TRANCS_4_STEP2_MMI(%[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                                   %[ftmp0])

        // dest high
        MMI_LWC1(%[ftmp9], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp10], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp11], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp12], %[addr0], 0x00)

        VC1_INV_TRANCS_4_STEP2_MMI(%[ftmp4], %[ftmp3], %[ftmp2], %[ftmp1],
                                   %[ftmp9], %[ftmp10], %[ftmp11], %[ftmp12],
                                   %[ftmp0])

        // dest low
        MMI_SWC1(%[ftmp5], %[dest], 0x00)
        PTR_ADDU   "%[addr0],   %[dest],    %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp6], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp7], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp8], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"

        // dest high
        MMI_SWC1(%[ftmp4], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp3], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp2], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp1], %[addr0], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),      [ftmp11]"=&f"(ftmp[11]),
          [ftmp12]"=&f"(ftmp[12]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          [addr0]"=&r"(addr[0]),
          [dest]"+&r"(dest)
        : [src]"r"(src),                [linesize]"r"(linesize),
          [ff_pw_1]"f"(ff_pw_1),        [ff_pw_4]"f"(ff_pw_4),
          [ff_pw_6]"f"(ff_pw_6),        [ff_pw_9]"f"(ff_pw_9),
          [ff_pw_12]"f"(ff_pw_12),      [ff_pw_15]"f"(ff_pw_15),
          [ff_pw_16]"f"(ff_pw_16),      [ff_pw_64]"f"(ff_pw_64)
        : "memory"
    );
}
#endif

/* Do inverse transform on 4x4 part of block */
void ff_vc1_inv_trans_4x4_dc_mmi(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
{
    int dc = block[0];
    double ftmp[5];
    DECLARE_VAR_LOW32;

    dc = (17 * dc +  4) >> 3;
    dc = (17 * dc + 64) >> 7;

    __asm__ volatile(
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]                \n\t"
        "pshufh     %[dc],      %[dc],          %[ftmp0]                \n\t"

        MMI_LWC1(%[ftmp1], %[dest0], 0x00)
        MMI_LWC1(%[ftmp2], %[dest1], 0x00)
        MMI_LWC1(%[ftmp3], %[dest2], 0x00)
        MMI_LWC1(%[ftmp4], %[dest3], 0x00)

        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"

        "paddsh     %[ftmp1],   %[ftmp1],       %[dc]                   \n\t"
        "paddsh     %[ftmp2],   %[ftmp2],       %[dc]                   \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[dc]                   \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[dc]                   \n\t"

        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]                \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp0]                \n\t"
        "packushb   %[ftmp3],   %[ftmp3],       %[ftmp0]                \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp0]                \n\t"

        MMI_SWC1(%[ftmp1], %[dest0], 0x00)
        MMI_SWC1(%[ftmp2], %[dest1], 0x00)
        MMI_SWC1(%[ftmp3], %[dest2], 0x00)
        MMI_SWC1(%[ftmp4], %[dest3], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          RESTRICT_ASM_LOW32
          [ftmp4]"=&f"(ftmp[4])
        : [dest0]"r"(dest+0*linesize),  [dest1]"r"(dest+1*linesize),
          [dest2]"r"(dest+2*linesize),  [dest3]"r"(dest+3*linesize),
          [dc]"f"(dc)
        : "memory"
    );
}

void ff_vc1_inv_trans_4x4_mmi(uint8_t *dest, ptrdiff_t linesize, int16_t *block)
{
    int16_t *src = block;
    int16_t *dst = block;
    double ftmp[12];
    uint32_t tmp[1];
    mips_reg addr[1];
    DECLARE_VAR_LOW32;

    // 1st loop
    __asm__ volatile (
        "li         %[tmp0],    0x03                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp0]                                \n\t"

        MMI_LDC1(%[ftmp1], %[src], 0x00)
        MMI_LDC1(%[ftmp2], %[src], 0x10)
        MMI_LDC1(%[ftmp3], %[src], 0x20)
        MMI_LDC1(%[ftmp4], %[src], 0x30)

        TRANSPOSE_4H(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                     %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                     %[ftmp9], %[tmp0],  %[ftmp10], %[ftmp11])

        //                              t1        t2        t3        t4
        VC1_INV_TRANCS_4_STEP1_MMI(%[ftmp1], %[ftmp3], %[ftmp4], %[ftmp2],
                                   %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ff_pw_17], %[ff_pw_10], %[ff_pw_22],
                                   %[ff_pw_4])

        PSRAH_4_MMI(%[ftmp1], %[ftmp3], %[ftmp4], %[ftmp2], %[ftmp0])

        TRANSPOSE_4H(%[ftmp1], %[ftmp3], %[ftmp4], %[ftmp2],
                     %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                     %[ftmp9], %[tmp0],  %[ftmp10], %[ftmp11])

        MMI_SDC1(%[ftmp1], %[dst], 0x00)
        MMI_SDC1(%[ftmp3], %[dst], 0x10)
        MMI_SDC1(%[ftmp4], %[dst], 0x20)
        MMI_SDC1(%[ftmp2], %[dst], 0x30)
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),      [ftmp11]"=&f"(ftmp[11]),
          [tmp0]"=&r"(tmp[0]),
          [src]"+&r"(src),              [dst]"+&r"(dst)
        : [ff_pw_17]"f"(ff_pw_17),      [ff_pw_10]"f"(ff_pw_10),
          [ff_pw_22]"f"(ff_pw_22),      [ff_pw_4]"f"(ff_pw_4)
        : "memory"
    );

    src = block;

    // 2nd loop
    __asm__ volatile (
        "li         %[tmp0],    0x07                                    \n\t"
        "mtc1       %[tmp0],    %[ftmp0]                                \n\t"

        // dest low 32bit
        MMI_LDC1(%[ftmp1], %[src], 0x00)
        MMI_LDC1(%[ftmp2], %[src], 0x20)
        MMI_LDC1(%[ftmp3], %[src], 0x30)
        MMI_LDC1(%[ftmp4], %[src], 0x10)

        VC1_INV_TRANCS_4_STEP1_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ff_pw_17], %[ff_pw_10], %[ff_pw_22],
                                   %[ff_pw_64])

        PSRAH_4_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4], %[ftmp0])

        MMI_LWC1(%[ftmp5], %[dest], 0x00)
        PTR_ADDU   "%[addr0],   %[dest],    %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp6], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp7], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_LWC1(%[ftmp8], %[addr0], 0x00)

        "xor        %[ftmp9],   %[ftmp9],  %[ftmp9]                     \n\t"

        VC1_INV_TRANCS_4_STEP2_MMI(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                                   %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                                   %[ftmp9])

        MMI_SWC1(%[ftmp1], %[dest], 0x00)
        PTR_ADDU   "%[addr0],   %[dest],    %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp2], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp3], %[addr0], 0x00)
        PTR_ADDU   "%[addr0],   %[addr0],   %[linesize]                 \n\t"
        MMI_SWC1(%[ftmp4], %[addr0], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          [addr0]"=&r"(addr[0])
        : [src]"r"(src),                [dest]"r"(dest),
          [linesize]"r"((mips_reg)linesize),
          [ff_pw_17]"f"(ff_pw_17),      [ff_pw_22]"f"(ff_pw_22),
          [ff_pw_10]"f"(ff_pw_10),      [ff_pw_64]"f"(ff_pw_64)
        : "memory"
    );
}

/* Apply overlap transform to horizontal edge */
void ff_vc1_h_overlap_mmi(uint8_t *src, int stride)
{
    int i;
    int a, b, c, d;
    int d1, d2;
    int rnd = 1;
    for (i = 0; i < 8; i++) {
        a  = src[-2];
        b  = src[-1];
        c  = src[0];
        d  = src[1];
        d1 = (a - d + 3 + rnd) >> 3;
        d2 = (a - d + b - c + 4 - rnd) >> 3;

        src[-2] = a - d1;
        src[-1] = av_clip_uint8(b - d2);
        src[0]  = av_clip_uint8(c + d2);
        src[1]  = d + d1;
        src    += stride;
        rnd     = !rnd;
    }
}

void ff_vc1_h_s_overlap_mmi(int16_t *left, int16_t *right)
{
    int i;
    int a, b, c, d;
    int d1, d2;
    int rnd1 = 4, rnd2 = 3;
    for (i = 0; i < 8; i++) {
        a  = left[6];
        b  = left[7];
        c  = right[0];
        d  = right[1];
        d1 = a - d;
        d2 = a - d + b - c;

        left[6]  = ((a << 3) - d1 + rnd1) >> 3;
        left[7]  = ((b << 3) - d2 + rnd2) >> 3;
        right[0] = ((c << 3) + d2 + rnd1) >> 3;
        right[1] = ((d << 3) + d1 + rnd2) >> 3;

        right += 8;
        left  += 8;
        rnd2   = 7 - rnd2;
        rnd1   = 7 - rnd1;
    }
}

/* Apply overlap transform to vertical edge */
void ff_vc1_v_overlap_mmi(uint8_t *src, int stride)
{
    int i;
    int a, b, c, d;
    int d1, d2;
    int rnd = 1;
    for (i = 0; i < 8; i++) {
        a  = src[-2 * stride];
        b  = src[-stride];
        c  = src[0];
        d  = src[stride];
        d1 = (a - d + 3 + rnd) >> 3;
        d2 = (a - d + b - c + 4 - rnd) >> 3;

        src[-2 * stride] = a - d1;
        src[-stride]     = av_clip_uint8(b - d2);
        src[0]           = av_clip_uint8(c + d2);
        src[stride]      = d + d1;
        src++;
        rnd = !rnd;
    }
}

void ff_vc1_v_s_overlap_mmi(int16_t *top, int16_t *bottom)
{
    int i;
    int a, b, c, d;
    int d1, d2;
    int rnd1 = 4, rnd2 = 3;
    for (i = 0; i < 8; i++) {
        a  = top[48];
        b  = top[56];
        c  = bottom[0];
        d  = bottom[8];
        d1 = a - d;
        d2 = a - d + b - c;

        top[48]   = ((a << 3) - d1 + rnd1) >> 3;
        top[56]   = ((b << 3) - d2 + rnd2) >> 3;
        bottom[0] = ((c << 3) + d2 + rnd1) >> 3;
        bottom[8] = ((d << 3) + d1 + rnd2) >> 3;

        bottom++;
        top++;
        rnd2 = 7 - rnd2;
        rnd1 = 7 - rnd1;
    }
}

/**
 * VC-1 in-loop deblocking filter for one line
 * @param src source block type
 * @param stride block stride
 * @param pq block quantizer
 * @return whether other 3 pairs should be filtered or not
 * @see 8.6
 */
static av_always_inline int vc1_filter_line(uint8_t *src, int stride, int pq)
{
    int a0 = (2 * (src[-2 * stride] - src[1 * stride]) -
              5 * (src[-1 * stride] - src[0 * stride]) + 4) >> 3;
    int a0_sign = a0 >> 31;        /* Store sign */

    a0 = (a0 ^ a0_sign) - a0_sign; /* a0 = FFABS(a0); */
    if (a0 < pq) {
        int a1 = FFABS((2 * (src[-4 * stride] - src[-1 * stride]) -
                        5 * (src[-3 * stride] - src[-2 * stride]) + 4) >> 3);
        int a2 = FFABS((2 * (src[ 0 * stride] - src[ 3 * stride]) -
                        5 * (src[ 1 * stride] - src[ 2 * stride]) + 4) >> 3);
        if (a1 < a0 || a2 < a0) {
            int clip      = src[-1 * stride] - src[0 * stride];
            int clip_sign = clip >> 31;

            clip = ((clip ^ clip_sign) - clip_sign) >> 1;
            if (clip) {
                int a3     = FFMIN(a1, a2);
                int d      = 5 * (a3 - a0);
                int d_sign = (d >> 31);

                d       = ((d ^ d_sign) - d_sign) >> 3;
                d_sign ^= a0_sign;

                if (d_sign ^ clip_sign)
                    d = 0;
                else {
                    d = FFMIN(d, clip);
                    d = (d ^ d_sign) - d_sign; /* Restore sign */
                    src[-1 * stride] = av_clip_uint8(src[-1 * stride] - d);
                    src[ 0 * stride] = av_clip_uint8(src[ 0 * stride] + d);
                }
                return 1;
            }
        }
    }
    return 0;
}

/**
 * VC-1 in-loop deblocking filter
 * @param src source block type
 * @param step distance between horizontally adjacent elements
 * @param stride distance between vertically adjacent elements
 * @param len edge length to filter (4 or 8 pixels)
 * @param pq block quantizer
 * @see 8.6
 */
static inline void vc1_loop_filter(uint8_t *src, int step, int stride,
                                   int len, int pq)
{
    int i;
    int filt3;

    for (i = 0; i < len; i += 4) {
        filt3 = vc1_filter_line(src + 2 * step, stride, pq);
        if (filt3) {
            vc1_filter_line(src + 0 * step, stride, pq);
            vc1_filter_line(src + 1 * step, stride, pq);
            vc1_filter_line(src + 3 * step, stride, pq);
        }
        src += step * 4;
    }
}

void ff_vc1_v_loop_filter4_mmi(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, 1, stride, 4, pq);
}

void ff_vc1_h_loop_filter4_mmi(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, stride, 1, 4, pq);
}

void ff_vc1_v_loop_filter8_mmi(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, 1, stride, 8, pq);
}

void ff_vc1_h_loop_filter8_mmi(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, stride, 1, 8, pq);
}

void ff_vc1_v_loop_filter16_mmi(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, 1, stride, 16, pq);
}

void ff_vc1_h_loop_filter16_mmi(uint8_t *src, int stride, int pq)
{
    vc1_loop_filter(src, stride, 1, 16, pq);
}

void ff_put_vc1_mspel_mc00_mmi(uint8_t *dst, const uint8_t *src,
                               ptrdiff_t stride, int rnd)
{
    ff_put_pixels8_8_mmi(dst, src, stride, 8);
}
void ff_put_vc1_mspel_mc00_16_mmi(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride, int rnd)
{
    ff_put_pixels16_8_mmi(dst, src, stride, 16);
}
void ff_avg_vc1_mspel_mc00_mmi(uint8_t *dst, const uint8_t *src,
                               ptrdiff_t stride, int rnd)
{
    ff_avg_pixels8_8_mmi(dst, src, stride, 8);
}
void ff_avg_vc1_mspel_mc00_16_mmi(uint8_t *dst, const uint8_t *src,
                                  ptrdiff_t stride, int rnd)
{
    ff_avg_pixels16_8_mmi(dst, src, stride, 16);
}

#define OP_PUT(S, D)
#define OP_AVG(S, D)                                                        \
    "ldc1       $f16,   "#S"                        \n\t"                   \
    "pavgb      "#D",   "#D",   $f16                \n\t"

/** Add rounder from $f14 to $f6 and pack result at destination */
#define NORMALIZE_MMI(SHIFT)                                                \
    "paddh      $f6,    $f6,    $f14                \n\t" /* +bias-r */     \
    "paddh      $f8,    $f8,    $f14                \n\t" /* +bias-r */     \
    "psrah      $f6,    $f6,    "SHIFT"             \n\t"                   \
    "psrah      $f8,    $f8,    "SHIFT"             \n\t"

#define TRANSFER_DO_PACK(OP)                                                \
    "packushb   $f6,    $f6,    $f8                 \n\t"                   \
    OP((%[dst]), $f6)                                                       \
    "sdc1       $f6,    0x00(%[dst])                \n\t"

#define TRANSFER_DONT_PACK(OP)                                              \
     OP(0(%[dst]), $f6)                                                     \
     OP(8(%[dst]), $f8)                                                     \
     "sdc1      $f6,    0x00(%[dst])                \n\t"                   \
     "sdc1      $f8,    0x08(%[dst])                \n\t"

/** @see MSPEL_FILTER13_CORE for use as UNPACK macro */
#define DO_UNPACK(reg)                                                      \
    "punpcklbh  "reg",  "reg",  $f0                 \n\t"
#define DONT_UNPACK(reg)

/** Compute the rounder 32-r or 8-r and unpacks it to $f14 */
#define LOAD_ROUNDER_MMI(ROUND)                                             \
    "lwc1       $f14,   "ROUND"                     \n\t"                   \
    "punpcklhw  $f14,   $f14,   $f14                \n\t"                   \
    "punpcklwd  $f14,   $f14,   $f14                \n\t"


#define SHIFT2_LINE(OFF, R0, R1, R2, R3)                                    \
    "paddh      "#R1",      "#R1",  "#R2"           \n\t"                   \
    PTR_ADDU    "$9,        %[src], %[stride1]      \n\t"                   \
    MMI_ULWC1(R0, $9, 0x00)                                                 \
    "pmullh     "#R1",      "#R1",  $f6             \n\t"                   \
    "punpcklbh  "#R0",      "#R0",  $f0             \n\t"                   \
    PTR_ADDU    "$9,        %[src], %[stride]       \n\t"                   \
    MMI_ULWC1(R3, $9, 0x00)                                                 \
    "psubh      "#R1",      "#R1",  "#R0"           \n\t"                   \
    "punpcklbh  "#R3",      "#R3",  $f0             \n\t"                   \
    "paddh      "#R1",      "#R1",  $f14            \n\t"                   \
    "psubh      "#R1",      "#R1",  "#R3"           \n\t"                   \
    "psrah      "#R1",      "#R1",  %[shift]        \n\t"                   \
    MMI_SDC1(R1, %[dst], OFF)                                               \
    PTR_ADDU    "%[src],    %[src], %[stride]       \n\t"

/** Sacrificing $f12 makes it possible to pipeline loads from src */
static void vc1_put_ver_16b_shift2_mmi(int16_t *dst,
                                       const uint8_t *src, mips_reg stride,
                                       int rnd, int64_t shift)
{
    DECLARE_VAR_LOW32;
    DECLARE_VAR_ADDRT;

    __asm__ volatile(
        "xor        $f0,    $f0,    $f0             \n\t"
        "li         $8,     0x03                    \n\t"
        LOAD_ROUNDER_MMI("%[rnd]")
        "ldc1       $f12,   %[ff_pw_9]              \n\t"
        "1:                                         \n\t"
        MMI_ULWC1($f4, %[src], 0x00)
        PTR_ADDU   "%[src], %[src], %[stride]       \n\t"
        MMI_ULWC1($f6, %[src], 0x00)
        "punpcklbh  $f4,    $f4,    $f0             \n\t"
        "punpcklbh  $f6,    $f6,    $f0             \n\t"
        SHIFT2_LINE(  0, $f2, $f4, $f6, $f8)
        SHIFT2_LINE( 24, $f4, $f6, $f8, $f2)
        SHIFT2_LINE( 48, $f6, $f8, $f2, $f4)
        SHIFT2_LINE( 72, $f8, $f2, $f4, $f6)
        SHIFT2_LINE( 96, $f2, $f4, $f6, $f8)
        SHIFT2_LINE(120, $f4, $f6, $f8, $f2)
        SHIFT2_LINE(144, $f6, $f8, $f2, $f4)
        SHIFT2_LINE(168, $f8, $f2, $f4, $f6)
        PTR_SUBU   "%[src], %[src], %[stride2]      \n\t"
        PTR_ADDIU  "%[dst], %[dst], 0x08            \n\t"
        "addiu      $8,     $8,    -0x01            \n\t"
        "bnez       $8,     1b                      \n\t"
        : RESTRICT_ASM_LOW32            RESTRICT_ASM_ADDRT
          [src]"+r"(src),               [dst]"+r"(dst)
        : [stride]"r"(stride),          [stride1]"r"(-2*stride),
          [shift]"f"(shift),            [rnd]"m"(rnd),
          [stride2]"r"(9*stride-4),     [ff_pw_9]"m"(ff_pw_9)
        : "$8", "$9", "$f0", "$f2", "$f4", "$f6", "$f8", "$f10", "$f12",
          "$f14", "$f16", "memory"
    );
}

/**
 * Data is already unpacked, so some operations can directly be made from
 * memory.
 */
#define VC1_HOR_16B_SHIFT2(OP, OPNAME)                                      \
static void OPNAME ## vc1_hor_16b_shift2_mmi(uint8_t *dst, mips_reg stride, \
                                             const int16_t *src, int rnd)   \
{                                                                           \
    int h = 8;                                                              \
    DECLARE_VAR_ALL64;                                                      \
    DECLARE_VAR_ADDRT;                                                      \
                                                                            \
    src -= 1;                                                               \
    rnd -= (-1+9+9-1)*1024; /* Add -1024 bias */                            \
                                                                            \
    __asm__ volatile(                                                       \
        LOAD_ROUNDER_MMI("%[rnd]")                                          \
        "ldc1       $f12,   %[ff_pw_128]            \n\t"                   \
        "ldc1       $f10,   %[ff_pw_9]              \n\t"                   \
        "1:                                         \n\t"                   \
        MMI_ULDC1($f2, %[src], 0x00)                                        \
        MMI_ULDC1($f4, %[src], 0x08)                                        \
        MMI_ULDC1($f6, %[src], 0x02)                                        \
        MMI_ULDC1($f8, %[src], 0x0a)                                        \
        MMI_ULDC1($f0, %[src], 0x06)                                        \
        "paddh      $f2,    $f2,    $f0             \n\t"                   \
        MMI_ULDC1($f0, %[src], 0x0e)                                        \
        "paddh      $f4,    $f4,    $f0             \n\t"                   \
        MMI_ULDC1($f0, %[src], 0x04)                                        \
        "paddh      $f6,    $f6,    $f0             \n\t"                   \
        MMI_ULDC1($f0, %[src], 0x0b)                                        \
        "paddh      $f8,    $f8,    $f0             \n\t"                   \
        "pmullh     $f6,    $f6,    $f10            \n\t"                   \
        "pmullh     $f8,    $f8,    $f10            \n\t"                   \
        "psubh      $f6,    $f6,    $f2             \n\t"                   \
        "psubh      $f8,    $f8,    $f4             \n\t"                   \
        "li         $8,     0x07                    \n\t"                   \
        "mtc1       $8,     $f16                    \n\t"                   \
        NORMALIZE_MMI("$f16")                                               \
        /* Remove bias */                                                   \
        "paddh      $f6,    $f6,    $f12            \n\t"                   \
        "paddh      $f8,    $f8,    $f12            \n\t"                   \
        TRANSFER_DO_PACK(OP)                                                \
        "addiu      %[h],   %[h],  -0x01            \n\t"                   \
        PTR_ADDIU  "%[src], %[src], 0x18            \n\t"                   \
        PTR_ADDU   "%[dst], %[dst], %[stride]       \n\t"                   \
        "bnez       %[h],   1b                      \n\t"                   \
        : RESTRICT_ASM_ALL64            RESTRICT_ASM_ADDRT                  \
          [h]"+r"(h),                                                       \
          [src]"+r"(src),               [dst]"+r"(dst)                      \
        : [stride]"r"(stride),          [rnd]"m"(rnd),                      \
          [ff_pw_9]"m"(ff_pw_9),        [ff_pw_128]"m"(ff_pw_128)           \
        : "$8", "$f0", "$f2", "$f4", "$f6", "$f8", "$f10", "$f12", "$f14",  \
          "$f16", "memory"                                                  \
    );                                                                      \
}

VC1_HOR_16B_SHIFT2(OP_PUT, put_)
VC1_HOR_16B_SHIFT2(OP_AVG, avg_)

/**
 * Purely vertical or horizontal 1/2 shift interpolation.
 * Sacrify $f12 for *9 factor.
 */
#define VC1_SHIFT2(OP, OPNAME)\
static void OPNAME ## vc1_shift2_mmi(uint8_t *dst, const uint8_t *src,      \
                                     mips_reg stride, int rnd,              \
                                     mips_reg offset)                       \
{                                                                           \
    DECLARE_VAR_LOW32;                                                      \
    DECLARE_VAR_ADDRT;                                                      \
                                                                            \
    rnd = 8 - rnd;                                                          \
                                                                            \
    __asm__ volatile(                                                       \
        "xor        $f0,    $f0,    $f0             \n\t"                   \
        "li         $10,    0x08                    \n\t"                   \
        LOAD_ROUNDER_MMI("%[rnd]")                                          \
        "ldc1       $f12,   %[ff_pw_9]              \n\t"                   \
        "1:                                         \n\t"                   \
        MMI_ULWC1($f6, %[src], 0x00)                                        \
        MMI_ULWC1($f8, %[src], 0x04)                                        \
        PTR_ADDU   "$9,     %[src], %[offset]       \n\t"                   \
        MMI_ULWC1($f2, $9, 0x00)                                            \
        MMI_ULWC1($f4, $9, 0x04)                                            \
        PTR_ADDU   "%[src], %[src], %[offset]       \n\t"                   \
        "punpcklbh  $f6,    $f6,    $f0             \n\t"                   \
        "punpcklbh  $f8,    $f8,    $f0             \n\t"                   \
        "punpcklbh  $f2,    $f2,    $f0             \n\t"                   \
        "punpcklbh  $f4,    $f4,    $f0             \n\t"                   \
        "paddh      $f6,    $f6,    $f2             \n\t"                   \
        "paddh      $f8,    $f8,    $f4             \n\t"                   \
        PTR_ADDU   "$9,     %[src], %[offset_x2n]   \n\t"                   \
        MMI_ULWC1($f2, $9, 0x00)                                            \
        MMI_ULWC1($f4, $9, 0x04)                                            \
        "pmullh     $f6,    $f6,    $f12            \n\t" /* 0,9,9,0*/      \
        "pmullh     $f8,    $f8,    $f12            \n\t" /* 0,9,9,0*/      \
        "punpcklbh  $f2,    $f2,    $f0             \n\t"                   \
        "punpcklbh  $f4,    $f4,    $f0             \n\t"                   \
        "psubh      $f6,    $f6,    $f2             \n\t" /*-1,9,9,0*/      \
        "psubh      $f8,    $f8,    $f4             \n\t" /*-1,9,9,0*/      \
        PTR_ADDU   "$9,     %[src], %[offset]       \n\t"                   \
        MMI_ULWC1($f2, $9, 0x00)                                            \
        MMI_ULWC1($f4, $9, 0x04)                                            \
        "punpcklbh  $f2,    $f2,    $f0             \n\t"                   \
        "punpcklbh  $f4,    $f4,    $f0             \n\t"                   \
        "psubh      $f6,    $f6,    $f2             \n\t" /*-1,9,9,-1*/     \
        "psubh      $f8,    $f8,    $f4             \n\t" /*-1,9,9,-1*/     \
        "li         $8,     0x04                    \n\t"                   \
        "mtc1       $8,     $f16                    \n\t"                   \
        NORMALIZE_MMI("$f16")                                               \
        "packushb   $f6,    $f6,    $f8             \n\t"                   \
        OP((%[dst]), $f6)                                                   \
        "sdc1       $f6,    0x00(%[dst])            \n\t"                   \
        "addiu      $10,    $10,   -0x01            \n\t"                   \
        PTR_ADDU   "%[src], %[src], %[stride1]      \n\t"                   \
        PTR_ADDU   "%[dst], %[dst], %[stride]       \n\t"                   \
        "bnez       $10,    1b                      \n\t"                   \
        : RESTRICT_ASM_LOW32            RESTRICT_ASM_ADDRT                  \
          [src]"+r"(src),               [dst]"+r"(dst)                      \
        : [offset]"r"(offset),          [offset_x2n]"r"(-2*offset),         \
          [stride]"g"(stride),          [rnd]"m"(rnd),                      \
          [stride1]"g"(stride-offset),                                      \
          [ff_pw_9]"m"(ff_pw_9)                                             \
        : "$8", "$9", "$10", "$f0", "$f2", "$f4", "$f6", "$f8", "$f10",     \
          "$f12", "$f14", "$f16", "memory"                                  \
    );                                                                      \
}

VC1_SHIFT2(OP_PUT, put_)
VC1_SHIFT2(OP_AVG, avg_)

/**
 * Core of the 1/4 and 3/4 shift bicubic interpolation.
 *
 * @param UNPACK  Macro unpacking arguments from 8 to 16bits (can be empty).
 * @param LOAD    "MMI_ULWC1" or "MMI_ULDC1", if data read is already unpacked.
 * @param M       "1" for MMI_ULWC1, "2" for MMI_ULDC1.
 * @param A1      Stride address of 1st tap (beware of unpacked/packed).
 * @param A2      Stride address of 2nd tap
 * @param A3      Stride address of 3rd tap
 * @param A4      Stride address of 4th tap
 */
#define MSPEL_FILTER13_CORE(UNPACK, LOAD, M, A1, A2, A3, A4)                \
    PTR_ADDU   "$9,     %[src], "#A1"           \n\t"                       \
    LOAD($f2, $9, M*0)                                                      \
    LOAD($f4, $9, M*4)                                                      \
    UNPACK("$f2")                                                           \
    UNPACK("$f4")                                                           \
    "pmullh     $f2,    $f2,    %[ff_pw_3]      \n\t"                       \
    "pmullh     $f4,    $f4,    %[ff_pw_3]      \n\t"                       \
    PTR_ADDU   "$9,     %[src], "#A2"           \n\t"                       \
    LOAD($f6, $9, M*0)                                                      \
    LOAD($f8, $9, M*4)                                                      \
    UNPACK("$f6")                                                           \
    UNPACK("$f8")                                                           \
    "pmullh     $f6,    $f6,    $f12            \n\t" /* *18 */             \
    "pmullh     $f8,    $f8,    $f12            \n\t" /* *18 */             \
    "psubh      $f6,    $f6,    $f2             \n\t" /* *18, -3 */         \
    "psubh      $f8,    $f8,    $f4             \n\t" /* *18, -3 */         \
    PTR_ADDU   "$9,     %[src], "#A4"           \n\t"                       \
    LOAD($f2, $9, M*0)                                                      \
    LOAD($f4, $9, M*4)                                                      \
    UNPACK("$f2")                                                           \
    UNPACK("$f4")                                                           \
    "li         $8,     0x02                    \n\t"                       \
    "mtc1       $8,     $f16                    \n\t"                       \
    "psllh      $f2,    $f2,    $f16            \n\t" /* 4* */              \
    "psllh      $f4,    $f4,    $f16            \n\t" /* 4* */              \
    "psubh      $f6,    $f6,    $f2             \n\t" /* -4,18,-3 */        \
    "psubh      $f8,    $f8,    $f4             \n\t" /* -4,18,-3 */        \
    PTR_ADDU   "$9,     %[src], "#A3"           \n\t"                       \
    LOAD($f2, $9, M*0)                                                      \
    LOAD($f4, $9, M*4)                                                      \
    UNPACK("$f2")                                                           \
    UNPACK("$f4")                                                           \
    "pmullh     $f2,    $f2,    $f10            \n\t" /* *53 */             \
    "pmullh     $f4,    $f4,    $f10            \n\t" /* *53 */             \
    "paddh      $f6,    $f6,    $f2             \n\t" /* 4,53,18,-3 */      \
    "paddh      $f8,    $f8,    $f4             \n\t" /* 4,53,18,-3 */

/**
 * Macro to build the vertical 16bits version of vc1_put_shift[13].
 * Here, offset=src_stride. Parameters passed A1 to A4 must use
 * %3 (src_stride), %4 (2*src_stride) and %5 (3*src_stride).
 *
 * @param  NAME   Either 1 or 3
 * @see MSPEL_FILTER13_CORE for information on A1->A4
 */
#define MSPEL_FILTER13_VER_16B(NAME, A1, A2, A3, A4)                        \
static void                                                                 \
vc1_put_ver_16b_ ## NAME ## _mmi(int16_t *dst, const uint8_t *src,          \
                                 mips_reg src_stride,                       \
                                 int rnd, int64_t shift)                    \
{                                                                           \
    int h = 8;                                                              \
    DECLARE_VAR_LOW32;                                                      \
    DECLARE_VAR_ADDRT;                                                      \
                                                                            \
    src -= src_stride;                                                      \
                                                                            \
    __asm__ volatile(                                                       \
        "xor        $f0,    $f0,    $f0             \n\t"                   \
        LOAD_ROUNDER_MMI("%[rnd]")                                          \
        "ldc1       $f10,   %[ff_pw_53]             \n\t"                   \
        "ldc1       $f12,   %[ff_pw_18]             \n\t"                   \
        ".p2align 3                                 \n\t"                   \
        "1:                                         \n\t"                   \
        MSPEL_FILTER13_CORE(DO_UNPACK, MMI_ULWC1, 1, A1, A2, A3, A4)        \
        NORMALIZE_MMI("%[shift]")                                           \
        TRANSFER_DONT_PACK(OP_PUT)                                          \
        /* Last 3 (in fact 4) bytes on the line */                          \
        PTR_ADDU   "$9,     %[src], "#A1"           \n\t"                   \
        MMI_ULWC1($f2, $9, 0x08)                                            \
        DO_UNPACK("$f2")                                                    \
        "mov.d      $f6,    $f2                     \n\t"                   \
        "paddh      $f2,    $f2,    $f2             \n\t"                   \
        "paddh      $f2,    $f2,    $f6             \n\t" /* 3* */          \
        PTR_ADDU   "$9,     %[src], "#A2"           \n\t"                   \
        MMI_ULWC1($f6, $9, 0x08)                                            \
        DO_UNPACK("$f6")                                                    \
        "pmullh     $f6,    $f6,    $f12            \n\t" /* *18 */         \
        "psubh      $f6,    $f6,    $f2             \n\t" /* *18,-3 */      \
        PTR_ADDU   "$9,     %[src], "#A3"           \n\t"                   \
        MMI_ULWC1($f2, $9, 0x08)                                            \
        DO_UNPACK("$f2")                                                    \
        "pmullh     $f2,    $f2,    $f10            \n\t" /* *53 */         \
        "paddh      $f6,    $f6,    $f2             \n\t" /* *53,18,-3 */   \
        PTR_ADDU   "$9,     %[src], "#A4"           \n\t"                   \
        MMI_ULWC1($f2, $9, 0x08)                                            \
        DO_UNPACK("$f2")                                                    \
        "li         $8,     0x02                    \n\t"                   \
        "mtc1       $8,     $f16                    \n\t"                   \
        "psllh      $f2,    $f2,    $f16            \n\t" /* 4* */          \
        "psubh      $f6,    $f6,    $f2             \n\t"                   \
        "paddh      $f6,    $f6,    $f14            \n\t"                   \
        "li         $8,     0x06                    \n\t"                   \
        "mtc1       $8,     $f16                    \n\t"                   \
        "psrah      $f6,    $f6,    $f16            \n\t"                   \
        "sdc1       $f6,    0x10(%[dst])            \n\t"                   \
        "addiu      %[h],   %[h],  -0x01            \n\t"                   \
        PTR_ADDU   "%[src], %[src], %[stride_x1]    \n\t"                   \
        PTR_ADDIU  "%[dst], %[dst], 0x18            \n\t"                   \
        "bnez       %[h],   1b                      \n\t"                   \
        : RESTRICT_ASM_LOW32            RESTRICT_ASM_ADDRT                  \
          [h]"+r"(h),                                                       \
          [src]"+r"(src),               [dst]"+r"(dst)                      \
        : [stride_x1]"r"(src_stride),   [stride_x2]"r"(2*src_stride),       \
          [stride_x3]"r"(3*src_stride),                                     \
          [rnd]"m"(rnd),                [shift]"f"(shift),                  \
          [ff_pw_53]"m"(ff_pw_53),      [ff_pw_18]"m"(ff_pw_18),            \
          [ff_pw_3]"f"(ff_pw_3)                                             \
        : "$8", "$9", "$f0", "$f2", "$f4", "$f6", "$f8", "$f10", "$f12",    \
          "$f14", "$f16", "memory"                                          \
    );                                                                      \
}

/**
 * Macro to build the horizontal 16bits version of vc1_put_shift[13].
 * Here, offset=16bits, so parameters passed A1 to A4 should be simple.
 *
 * @param  NAME   Either 1 or 3
 * @see MSPEL_FILTER13_CORE for information on A1->A4
 */
#define MSPEL_FILTER13_HOR_16B(NAME, A1, A2, A3, A4, OP, OPNAME)            \
static void                                                                 \
OPNAME ## vc1_hor_16b_ ## NAME ## _mmi(uint8_t *dst, mips_reg stride,       \
                                       const int16_t *src, int rnd)         \
{                                                                           \
    int h = 8;                                                              \
    DECLARE_VAR_ALL64;                                                      \
    DECLARE_VAR_ADDRT;                                                      \
                                                                            \
    src -= 1;                                                               \
    rnd -= (-4+58+13-3)*256; /* Add -256 bias */                            \
                                                                            \
    __asm__ volatile(                                                       \
        "xor        $f0,    $f0,    $f0             \n\t"                   \
        LOAD_ROUNDER_MMI("%[rnd]")                                          \
        "ldc1       $f10,   %[ff_pw_53]             \n\t"                   \
        "ldc1       $f12,   %[ff_pw_18]             \n\t"                   \
        ".p2align 3                                 \n\t"                   \
        "1:                                         \n\t"                   \
        MSPEL_FILTER13_CORE(DONT_UNPACK, MMI_ULDC1, 2, A1, A2, A3, A4)      \
        "li         $8,     0x07                    \n\t"                   \
        "mtc1       $8,     $f16                    \n\t"                   \
        NORMALIZE_MMI("$f16")                                               \
        /* Remove bias */                                                   \
        "paddh      $f6,    $f6,    %[ff_pw_128]    \n\t"                   \
        "paddh      $f8,    $f8,    %[ff_pw_128]    \n\t"                   \
        TRANSFER_DO_PACK(OP)                                                \
        "addiu      %[h],   %[h],  -0x01            \n\t"                   \
        PTR_ADDU   "%[src], %[src], 0x18            \n\t"                   \
        PTR_ADDU   "%[dst], %[dst], %[stride]       \n\t"                   \
        "bnez       %[h],   1b                      \n\t"                   \
        : RESTRICT_ASM_ALL64            RESTRICT_ASM_ADDRT                  \
          [h]"+r"(h),                                                       \
          [src]"+r"(src),               [dst]"+r"(dst)                      \
        : [stride]"r"(stride),          [rnd]"m"(rnd),                      \
          [ff_pw_53]"m"(ff_pw_53),      [ff_pw_18]"m"(ff_pw_18),            \
          [ff_pw_3]"f"(ff_pw_3),        [ff_pw_128]"f"(ff_pw_128)           \
        : "$8", "$9", "$f0", "$f2", "$f4", "$f6", "$f8", "$f10", "$f12",    \
          "$f14", "$f16", "memory"                                          \
    );                                                                      \
}

/**
 * Macro to build the 8bits, any direction, version of vc1_put_shift[13].
 * Here, offset=src_stride. Parameters passed A1 to A4 must use
 * %3 (offset), %4 (2*offset) and %5 (3*offset).
 *
 * @param  NAME   Either 1 or 3
 * @see MSPEL_FILTER13_CORE for information on A1->A4
 */
#define MSPEL_FILTER13_8B(NAME, A1, A2, A3, A4, OP, OPNAME)                 \
static void                                                                 \
OPNAME ## vc1_## NAME ## _mmi(uint8_t *dst, const uint8_t *src,             \
                              mips_reg stride, int rnd, mips_reg offset)    \
{                                                                           \
    int h = 8;                                                              \
    DECLARE_VAR_LOW32;                                                      \
    DECLARE_VAR_ADDRT;                                                      \
                                                                            \
    src -= offset;                                                          \
    rnd = 32-rnd;                                                           \
                                                                            \
    __asm__ volatile (                                                      \
        "xor        $f0,    $f0,    $f0             \n\t"                   \
        LOAD_ROUNDER_MMI("%[rnd]")                                          \
        "ldc1       $f10,   %[ff_pw_53]             \n\t"                   \
        "ldc1       $f12,   %[ff_pw_18]             \n\t"                   \
        ".p2align 3                                 \n\t"                   \
        "1:                                         \n\t"                   \
        MSPEL_FILTER13_CORE(DO_UNPACK, MMI_ULWC1, 1, A1, A2, A3, A4)        \
        "li         $8,     0x06                    \n\t"                   \
        "mtc1       $8,     $f16                    \n\t"                   \
        NORMALIZE_MMI("$f16")                                               \
        TRANSFER_DO_PACK(OP)                                                \
        "addiu      %[h],   %[h],      -0x01        \n\t"                   \
        PTR_ADDU   "%[src], %[src],     %[stride]   \n\t"                   \
        PTR_ADDU   "%[dst], %[dst],     %[stride]   \n\t"                   \
        "bnez       %[h],   1b                      \n\t"                   \
        : RESTRICT_ASM_LOW32            RESTRICT_ASM_ADDRT                  \
          [h]"+r"(h),                                                       \
          [src]"+r"(src),               [dst]"+r"(dst)                      \
        : [offset_x1]"r"(offset),       [offset_x2]"r"(2*offset),           \
          [offset_x3]"r"(3*offset),     [stride]"g"(stride),                \
          [rnd]"m"(rnd),                                                    \
          [ff_pw_53]"m"(ff_pw_53),      [ff_pw_18]"m"(ff_pw_18),            \
          [ff_pw_3]"f"(ff_pw_3)                                             \
        : "$8", "$9", "$f0", "$f2", "$f4", "$f6", "$f8", "$f10", "$f12",    \
          "$f14", "$f16", "memory"                                          \
    );                                                                      \
}


/** 1/4 shift bicubic interpolation */
MSPEL_FILTER13_8B(shift1, %[offset_x3], %[offset_x2], %[offset_x1], $0, OP_PUT, put_)
MSPEL_FILTER13_8B(shift1, %[offset_x3], %[offset_x2], %[offset_x1], $0, OP_AVG, avg_)
MSPEL_FILTER13_VER_16B(shift1, %[stride_x3], %[stride_x2], %[stride_x1], $0)
MSPEL_FILTER13_HOR_16B(shift1, 6, 4, 2, 0, OP_PUT, put_)
MSPEL_FILTER13_HOR_16B(shift1, 6, 4, 2, 0, OP_AVG, avg_)

/** 3/4 shift bicubic interpolation */
MSPEL_FILTER13_8B(shift3, $0, %[offset_x1], %[offset_x2], %[offset_x3], OP_PUT, put_)
MSPEL_FILTER13_8B(shift3, $0, %[offset_x1], %[offset_x2], %[offset_x3], OP_AVG, avg_)
MSPEL_FILTER13_VER_16B(shift3, $0, %[stride_x1], %[stride_x2], %[stride_x3])
MSPEL_FILTER13_HOR_16B(shift3, 0, 2, 4, 6, OP_PUT, put_)
MSPEL_FILTER13_HOR_16B(shift3, 0, 2, 4, 6, OP_AVG, avg_)

typedef void (*vc1_mspel_mc_filter_ver_16bits)
             (int16_t *dst, const uint8_t *src, mips_reg src_stride, int rnd,
              int64_t shift);
typedef void (*vc1_mspel_mc_filter_hor_16bits)
             (uint8_t *dst, mips_reg dst_stride, const int16_t *src, int rnd);
typedef void (*vc1_mspel_mc_filter_8bits)
             (uint8_t *dst, const uint8_t *src, mips_reg stride, int rnd,
              mips_reg offset);

/**
 * Interpolate fractional pel values by applying proper vertical then
 * horizontal filter.
 *
 * @param  dst     Destination buffer for interpolated pels.
 * @param  src     Source buffer.
 * @param  stride  Stride for both src and dst buffers.
 * @param  hmode   Horizontal filter (expressed in quarter pixels shift).
 * @param  hmode   Vertical filter.
 * @param  rnd     Rounding bias.
 */
#define VC1_MSPEL_MC(OP)                                                    \
static void OP ## vc1_mspel_mc(uint8_t *dst, const uint8_t *src, int stride,\
                               int hmode, int vmode, int rnd)               \
{                                                                           \
    static const vc1_mspel_mc_filter_ver_16bits vc1_put_shift_ver_16bits[] =\
         { NULL, vc1_put_ver_16b_shift1_mmi,                                \
                 vc1_put_ver_16b_shift2_mmi,                                \
                 vc1_put_ver_16b_shift3_mmi };                              \
    static const vc1_mspel_mc_filter_hor_16bits vc1_put_shift_hor_16bits[] =\
         { NULL, OP ## vc1_hor_16b_shift1_mmi,                              \
                 OP ## vc1_hor_16b_shift2_mmi,                              \
                 OP ## vc1_hor_16b_shift3_mmi };                            \
    static const vc1_mspel_mc_filter_8bits vc1_put_shift_8bits[] =          \
         { NULL, OP ## vc1_shift1_mmi,                                      \
                 OP ## vc1_shift2_mmi,                                      \
                 OP ## vc1_shift3_mmi };                                    \
                                                                            \
    if (vmode) { /* Vertical filter to apply */                             \
        if (hmode) { /* Horizontal filter to apply, output to tmp */        \
            static const int shift_value[] = { 0, 5, 1, 5 };                \
            int    shift = (shift_value[hmode]+shift_value[vmode])>>1;      \
            int    r;                                                       \
            LOCAL_ALIGNED(16, int16_t, tmp, [12*8]);                        \
                                                                            \
            r = (1<<(shift-1)) + rnd-1;                                     \
            vc1_put_shift_ver_16bits[vmode](tmp, src-1, stride, r, shift);  \
                                                                            \
            vc1_put_shift_hor_16bits[hmode](dst, stride, tmp+1, 64-rnd);    \
            return;                                                         \
        }                                                                   \
        else { /* No horizontal filter, output 8 lines to dst */            \
            vc1_put_shift_8bits[vmode](dst, src, stride, 1-rnd, stride);    \
            return;                                                         \
        }                                                                   \
    }                                                                       \
                                                                            \
    /* Horizontal mode with no vertical mode */                             \
    vc1_put_shift_8bits[hmode](dst, src, stride, rnd, 1);                   \
}                                                                           \
static void OP ## vc1_mspel_mc_16(uint8_t *dst, const uint8_t *src,         \
                                  int stride, int hmode, int vmode, int rnd)\
{                                                                           \
    OP ## vc1_mspel_mc(dst + 0, src + 0, stride, hmode, vmode, rnd);        \
    OP ## vc1_mspel_mc(dst + 8, src + 8, stride, hmode, vmode, rnd);        \
    dst += 8*stride; src += 8*stride;                                       \
    OP ## vc1_mspel_mc(dst + 0, src + 0, stride, hmode, vmode, rnd);        \
    OP ## vc1_mspel_mc(dst + 8, src + 8, stride, hmode, vmode, rnd);        \
}

VC1_MSPEL_MC(put_)
VC1_MSPEL_MC(avg_)

/** Macro to ease bicubic filter interpolation functions declarations */
#define DECLARE_FUNCTION(a, b)                                              \
void ff_put_vc1_mspel_mc ## a ## b ## _mmi(uint8_t *dst,                    \
                                           const uint8_t *src,              \
                                           ptrdiff_t stride,                \
                                           int rnd)                         \
{                                                                           \
     put_vc1_mspel_mc(dst, src, stride, a, b, rnd);                         \
}                                                                           \
void ff_avg_vc1_mspel_mc ## a ## b ## _mmi(uint8_t *dst,                    \
                                           const uint8_t *src,              \
                                           ptrdiff_t stride,                \
                                           int rnd)                         \
{                                                                           \
     avg_vc1_mspel_mc(dst, src, stride, a, b, rnd);                         \
}                                                                           \
void ff_put_vc1_mspel_mc ## a ## b ## _16_mmi(uint8_t *dst,                 \
                                              const uint8_t *src,           \
                                              ptrdiff_t stride,             \
                                              int rnd)                      \
{                                                                           \
     put_vc1_mspel_mc_16(dst, src, stride, a, b, rnd);                      \
}                                                                           \
void ff_avg_vc1_mspel_mc ## a ## b ## _16_mmi(uint8_t *dst,                 \
                                              const uint8_t *src,           \
                                              ptrdiff_t stride,             \
                                              int rnd)                      \
{                                                                           \
     avg_vc1_mspel_mc_16(dst, src, stride, a, b, rnd);                      \
}

DECLARE_FUNCTION(0, 1)
DECLARE_FUNCTION(0, 2)
DECLARE_FUNCTION(0, 3)

DECLARE_FUNCTION(1, 0)
DECLARE_FUNCTION(1, 1)
DECLARE_FUNCTION(1, 2)
DECLARE_FUNCTION(1, 3)

DECLARE_FUNCTION(2, 0)
DECLARE_FUNCTION(2, 1)
DECLARE_FUNCTION(2, 2)
DECLARE_FUNCTION(2, 3)

DECLARE_FUNCTION(3, 0)
DECLARE_FUNCTION(3, 1)
DECLARE_FUNCTION(3, 2)
DECLARE_FUNCTION(3, 3)

#define CHROMA_MC_8_MMI                                                     \
        "punpckhbh  %[ftmp5],   %[ftmp1],   %[ftmp0]                \n\t"   \
        "punpcklbh  %[ftmp1],   %[ftmp1],   %[ftmp0]                \n\t"   \
        "punpckhbh  %[ftmp6],   %[ftmp2],   %[ftmp0]                \n\t"   \
        "punpcklbh  %[ftmp2],   %[ftmp2],   %[ftmp0]                \n\t"   \
        "punpckhbh  %[ftmp7],   %[ftmp3],   %[ftmp0]                \n\t"   \
        "punpcklbh  %[ftmp3],   %[ftmp3],   %[ftmp0]                \n\t"   \
        "punpckhbh  %[ftmp8],   %[ftmp4],   %[ftmp0]                \n\t"   \
        "punpcklbh  %[ftmp4],   %[ftmp4],   %[ftmp0]                \n\t"   \
                                                                            \
        "pmullh     %[ftmp1],   %[ftmp1],   %[A]                    \n\t"   \
        "pmullh     %[ftmp5],   %[ftmp5],   %[A]                    \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],   %[B]                    \n\t"   \
        "pmullh     %[ftmp6],   %[ftmp6],   %[B]                    \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],   %[C]                    \n\t"   \
        "pmullh     %[ftmp7],   %[ftmp7],   %[C]                    \n\t"   \
        "pmullh     %[ftmp4],   %[ftmp4],   %[D]                    \n\t"   \
        "pmullh     %[ftmp8],   %[ftmp8],   %[D]                    \n\t"   \
                                                                            \
        "paddh      %[ftmp1],   %[ftmp1],   %[ftmp2]                \n\t"   \
        "paddh      %[ftmp3],   %[ftmp3],   %[ftmp4]                \n\t"   \
        "paddh      %[ftmp1],   %[ftmp1],   %[ftmp3]                \n\t"   \
        "paddh      %[ftmp1],   %[ftmp1],   %[ff_pw_28]             \n\t"   \
                                                                            \
        "paddh      %[ftmp5],   %[ftmp5],   %[ftmp6]                \n\t"   \
        "paddh      %[ftmp7],   %[ftmp7],   %[ftmp8]                \n\t"   \
        "paddh      %[ftmp5],   %[ftmp5],   %[ftmp7]                \n\t"   \
        "paddh      %[ftmp5],   %[ftmp5],   %[ff_pw_28]             \n\t"   \
                                                                            \
        "psrlh      %[ftmp1],   %[ftmp1],   %[ftmp9]                \n\t"   \
        "psrlh      %[ftmp5],   %[ftmp5],   %[ftmp9]                \n\t"   \
        "packushb   %[ftmp1],   %[ftmp1],   %[ftmp5]                \n\t"


#define CHROMA_MC_4_MMI                                                     \
        "punpcklbh  %[ftmp1],   %[ftmp1],   %[ftmp0]                \n\t"   \
        "punpcklbh  %[ftmp2],   %[ftmp2],   %[ftmp0]                \n\t"   \
        "punpcklbh  %[ftmp3],   %[ftmp3],   %[ftmp0]                \n\t"   \
        "punpcklbh  %[ftmp4],   %[ftmp4],   %[ftmp0]                \n\t"   \
                                                                            \
        "pmullh     %[ftmp1],   %[ftmp1],   %[A]                    \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],   %[B]                    \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],   %[C]                    \n\t"   \
        "pmullh     %[ftmp4],   %[ftmp4],   %[D]                    \n\t"   \
                                                                            \
        "paddh      %[ftmp1],   %[ftmp1],   %[ftmp2]                \n\t"   \
        "paddh      %[ftmp3],   %[ftmp3],   %[ftmp4]                \n\t"   \
        "paddh      %[ftmp1],   %[ftmp1],   %[ftmp3]                \n\t"   \
        "paddh      %[ftmp1],   %[ftmp1],   %[ff_pw_28]             \n\t"   \
                                                                            \
        "psrlh      %[ftmp1],   %[ftmp1],   %[ftmp5]                \n\t"   \
        "packushb   %[ftmp1],   %[ftmp1],   %[ftmp0]                \n\t"


void ff_put_no_rnd_vc1_chroma_mc8_mmi(uint8_t *dst /* align 8 */,
                                      uint8_t *src /* align 1 */,
                                      int stride, int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B =     (x) * (8 - y);
    const int C = (8 - x) *     (y);
    const int D =     (x) *     (y);
    double ftmp[10];
    uint32_t tmp[1];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    __asm__ volatile(
        "li         %[tmp0],    0x06                                    \n\t"
        "xor        %[ftmp0],   %[ftmp0],   %[ftmp0]                    \n\t"
        "mtc1       %[tmp0],    %[ftmp9]                                \n\t"
        "pshufh     %[A],       %[A],       %[ftmp0]                    \n\t"
        "pshufh     %[B],       %[B],       %[ftmp0]                    \n\t"
        "pshufh     %[C],       %[C],       %[ftmp0]                    \n\t"
        "pshufh     %[D],       %[D],       %[ftmp0]                    \n\t"

        "1:                                                             \n\t"
        MMI_ULDC1(%[ftmp1], %[src], 0x00)
        MMI_ULDC1(%[ftmp2], %[src], 0x01)
        PTR_ADDU   "%[src],     %[src],     %[stride]                   \n\t"
        MMI_ULDC1(%[ftmp3], %[src], 0x00)
        MMI_ULDC1(%[ftmp4], %[src], 0x01)

        CHROMA_MC_8_MMI

        MMI_SDC1(%[ftmp1], %[dst], 0x00)
        "addiu      %[h],       %[h],      -0x01                        \n\t"
        PTR_ADDU   "%[dst],     %[dst],     %[stride]                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [tmp0]"=&r"(tmp[0]),
          [src]"+&r"(src),              [dst]"+&r"(dst),
          [h]"+&r"(h)
        : [stride]"r"((mips_reg)stride),
          [A]"f"(A),                    [B]"f"(B),
          [C]"f"(C),                    [D]"f"(D),
          [ff_pw_28]"f"(ff_pw_28)
        : "memory"
    );
}

void ff_put_no_rnd_vc1_chroma_mc4_mmi(uint8_t *dst /* align 8 */,
                                      uint8_t *src /* align 1 */,
                                      int stride, int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B =     (x) * (8 - y);
    const int C = (8 - x) *     (y);
    const int D =     (x) *     (y);
    double ftmp[6];
    uint32_t tmp[1];
    DECLARE_VAR_LOW32;
    DECLARE_VAR_ADDRT;

    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    __asm__ volatile(
        "li         %[tmp0],    0x06                                    \n\t"
        "xor        %[ftmp0],   %[ftmp0],   %[ftmp0]                    \n\t"
        "mtc1       %[tmp0],    %[ftmp5]                                \n\t"
        "pshufh     %[A],       %[A],       %[ftmp0]                    \n\t"
        "pshufh     %[B],       %[B],       %[ftmp0]                    \n\t"
        "pshufh     %[C],       %[C],       %[ftmp0]                    \n\t"
        "pshufh     %[D],       %[D],       %[ftmp0]                    \n\t"

        "1:                                                             \n\t"
        MMI_ULWC1(%[ftmp1], %[src], 0x00)
        MMI_ULWC1(%[ftmp2], %[src], 0x01)
        PTR_ADDU   "%[src],     %[src],     %[stride]                   \n\t"
        MMI_ULWC1(%[ftmp3], %[src], 0x00)
        MMI_ULWC1(%[ftmp4], %[src], 0x01)

        CHROMA_MC_4_MMI

        MMI_SWC1(%[ftmp1], %[dst], 0x00)
        "addiu      %[h],       %[h],      -0x01                        \n\t"
        PTR_ADDU   "%[dst],     %[dst],     %[stride]                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          RESTRICT_ASM_ADDRT
          [src]"+&r"(src),              [dst]"+&r"(dst),
          [h]"+&r"(h)
        : [stride]"r"((mips_reg)stride),
          [A]"f"(A),                    [B]"f"(B),
          [C]"f"(C),                    [D]"f"(D),
          [ff_pw_28]"f"(ff_pw_28)
        : "memory"
    );
}

void ff_avg_no_rnd_vc1_chroma_mc8_mmi(uint8_t *dst /* align 8 */,
                                      uint8_t *src /* align 1 */,
                                      int stride, int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B =     (x) * (8 - y);
    const int C = (8 - x) *     (y);
    const int D =     (x) *     (y);
    double ftmp[10];
    uint32_t tmp[1];
    DECLARE_VAR_ALL64;
    DECLARE_VAR_ADDRT;

    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    __asm__ volatile(
        "li         %[tmp0],    0x06                                    \n\t"
        "xor        %[ftmp0],   %[ftmp0],   %[ftmp0]                    \n\t"
        "mtc1       %[tmp0],    %[ftmp9]                                \n\t"
        "pshufh     %[A],       %[A],       %[ftmp0]                    \n\t"
        "pshufh     %[B],       %[B],       %[ftmp0]                    \n\t"
        "pshufh     %[C],       %[C],       %[ftmp0]                    \n\t"
        "pshufh     %[D],       %[D],       %[ftmp0]                    \n\t"

        "1:                                                             \n\t"
        MMI_ULDC1(%[ftmp1], %[src], 0x00)
        MMI_ULDC1(%[ftmp2], %[src], 0x01)
        PTR_ADDU   "%[src],     %[src],     %[stride]                   \n\t"
        MMI_ULDC1(%[ftmp3], %[src], 0x00)
        MMI_ULDC1(%[ftmp4], %[src], 0x01)

        CHROMA_MC_8_MMI

        MMI_LDC1(%[ftmp2], %[dst], 0x00)
        "pavgb      %[ftmp1],   %[ftmp1],   %[ftmp2]                    \n\t"

        MMI_SDC1(%[ftmp1], %[dst], 0x00)
        "addiu      %[h],       %[h],      -0x01                        \n\t"
        PTR_ADDU   "%[dst],     %[dst],     %[stride]                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),        [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),        [ftmp9]"=&f"(ftmp[9]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          RESTRICT_ASM_ADDRT
          [src]"+&r"(src),              [dst]"+&r"(dst),
          [h]"+&r"(h)
        : [stride]"r"((mips_reg)stride),
          [A]"f"(A),                    [B]"f"(B),
          [C]"f"(C),                    [D]"f"(D),
          [ff_pw_28]"f"(ff_pw_28)
        : "memory"
    );
}

void ff_avg_no_rnd_vc1_chroma_mc4_mmi(uint8_t *dst /* align 8 */,
                                      uint8_t *src /* align 1 */,
                                      int stride, int h, int x, int y)
{
    const int A = (8 - x) * (8 - y);
    const int B = (    x) * (8 - y);
    const int C = (8 - x) * (    y);
    const int D = (    x) * (    y);
    double ftmp[6];
    uint32_t tmp[1];
    DECLARE_VAR_LOW32;
    DECLARE_VAR_ADDRT;

    av_assert2(x < 8 && y < 8 && x >= 0 && y >= 0);

    __asm__ volatile(
        "li         %[tmp0],    0x06                                    \n\t"
        "xor        %[ftmp0],   %[ftmp0],   %[ftmp0]                    \n\t"
        "mtc1       %[tmp0],    %[ftmp5]                                \n\t"
        "pshufh     %[A],       %[A],       %[ftmp0]                    \n\t"
        "pshufh     %[B],       %[B],       %[ftmp0]                    \n\t"
        "pshufh     %[C],       %[C],       %[ftmp0]                    \n\t"
        "pshufh     %[D],       %[D],       %[ftmp0]                    \n\t"

        "1:                                                             \n\t"
        MMI_ULWC1(%[ftmp1], %[src], 0x00)
        MMI_ULWC1(%[ftmp2], %[src], 0x01)
        PTR_ADDU   "%[src],     %[src],     %[stride]                   \n\t"
        MMI_ULWC1(%[ftmp3], %[src], 0x00)
        MMI_ULWC1(%[ftmp4], %[src], 0x01)

        CHROMA_MC_4_MMI

        MMI_LWC1(%[ftmp2], %[dst], 0x00)
        "pavgb      %[ftmp1],   %[ftmp1],   %[ftmp2]                    \n\t"

        MMI_SWC1(%[ftmp1], %[dst], 0x00)
        "addiu      %[h],       %[h],      -0x01                        \n\t"
        PTR_ADDU   "%[dst],     %[dst],     %[stride]                   \n\t"
        "bnez       %[h],       1b                                      \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          RESTRICT_ASM_ADDRT
          [src]"+&r"(src),              [dst]"+&r"(dst),
          [h]"+&r"(h)
        : [stride]"r"((mips_reg)stride),
          [A]"f"(A),                    [B]"f"(B),
          [C]"f"(C),                    [D]"f"(D),
          [ff_pw_28]"f"(ff_pw_28)
        : "memory"
    );
}
