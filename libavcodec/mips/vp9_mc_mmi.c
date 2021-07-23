/*
 * Copyright (c) 2019 gxw <guxiwei-hf@loongson.cn>
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

#include "libavcodec/vp9dsp.h"
#include "libavutil/mips/mmiutils.h"
#include "vp9dsp_mips.h"

#define GET_DATA_H_MMI                                       \
    "pmaddhw    %[ftmp4],    %[ftmp4],   %[filter1]    \n\t" \
    "pmaddhw    %[ftmp5],    %[ftmp5],   %[filter2]    \n\t" \
    "paddw      %[ftmp4],    %[ftmp4],   %[ftmp5]      \n\t" \
    "punpckhwd  %[ftmp5],    %[ftmp4],   %[ftmp0]      \n\t" \
    "paddw      %[ftmp4],    %[ftmp4],   %[ftmp5]      \n\t" \
    "pmaddhw    %[ftmp6],    %[ftmp6],   %[filter1]    \n\t" \
    "pmaddhw    %[ftmp7],    %[ftmp7],   %[filter2]    \n\t" \
    "paddw      %[ftmp6],    %[ftmp6],   %[ftmp7]      \n\t" \
    "punpckhwd  %[ftmp7],    %[ftmp6],   %[ftmp0]      \n\t" \
    "paddw      %[ftmp6],    %[ftmp6],   %[ftmp7]      \n\t" \
    "punpcklwd  %[srcl],     %[ftmp4],   %[ftmp6]      \n\t" \
    "pmaddhw    %[ftmp8],    %[ftmp8],   %[filter1]    \n\t" \
    "pmaddhw    %[ftmp9],    %[ftmp9],   %[filter2]    \n\t" \
    "paddw      %[ftmp8],    %[ftmp8],   %[ftmp9]      \n\t" \
    "punpckhwd  %[ftmp9],    %[ftmp8],   %[ftmp0]      \n\t" \
    "paddw      %[ftmp8],    %[ftmp8],   %[ftmp9]      \n\t" \
    "pmaddhw    %[ftmp10],   %[ftmp10],  %[filter1]    \n\t" \
    "pmaddhw    %[ftmp11],   %[ftmp11],  %[filter2]    \n\t" \
    "paddw      %[ftmp10],   %[ftmp10],  %[ftmp11]     \n\t" \
    "punpckhwd  %[ftmp11],   %[ftmp10],  %[ftmp0]      \n\t" \
    "paddw      %[ftmp10],   %[ftmp10],  %[ftmp11]     \n\t" \
    "punpcklwd  %[srch],     %[ftmp8],   %[ftmp10]     \n\t"

#define GET_DATA_V_MMI                                       \
    "punpcklhw  %[srcl],     %[ftmp4],   %[ftmp5]      \n\t" \
    "pmaddhw    %[srcl],     %[srcl],    %[filter10]   \n\t" \
    "punpcklhw  %[ftmp12],   %[ftmp6],   %[ftmp7]      \n\t" \
    "pmaddhw    %[ftmp12],   %[ftmp12],  %[filter32]   \n\t" \
    "paddw      %[srcl],     %[srcl],    %[ftmp12]     \n\t" \
    "punpcklhw  %[ftmp12],   %[ftmp8],   %[ftmp9]      \n\t" \
    "pmaddhw    %[ftmp12],   %[ftmp12],  %[filter54]   \n\t" \
    "paddw      %[srcl],     %[srcl],    %[ftmp12]     \n\t" \
    "punpcklhw  %[ftmp12],   %[ftmp10],  %[ftmp11]     \n\t" \
    "pmaddhw    %[ftmp12],   %[ftmp12],  %[filter76]   \n\t" \
    "paddw      %[srcl],     %[srcl],    %[ftmp12]     \n\t" \
    "punpckhhw  %[srch],     %[ftmp4],   %[ftmp5]      \n\t" \
    "pmaddhw    %[srch],     %[srch],    %[filter10]   \n\t" \
    "punpckhhw  %[ftmp12],   %[ftmp6],   %[ftmp7]      \n\t" \
    "pmaddhw    %[ftmp12],   %[ftmp12],  %[filter32]   \n\t" \
    "paddw      %[srch],     %[srch],    %[ftmp12]     \n\t" \
    "punpckhhw  %[ftmp12],   %[ftmp8],   %[ftmp9]      \n\t" \
    "pmaddhw    %[ftmp12],   %[ftmp12],  %[filter54]   \n\t" \
    "paddw      %[srch],     %[srch],    %[ftmp12]     \n\t" \
    "punpckhhw  %[ftmp12],   %[ftmp10],  %[ftmp11]     \n\t" \
    "pmaddhw    %[ftmp12],   %[ftmp12],  %[filter76]   \n\t" \
    "paddw      %[srch],     %[srch],    %[ftmp12]     \n\t"

static void convolve_horiz_mmi(const uint8_t *src, int32_t src_stride,
                               uint8_t *dst, int32_t dst_stride,
                               const uint16_t *filter_x, int32_t w,
                               int32_t h)
{
    double ftmp[15];
    uint32_t tmp[2];
    DECLARE_VAR_ALL64;
    src -= 3;
    src_stride -= w;
    dst_stride -= w;
    __asm__ volatile (
        "move       %[tmp1],    %[width]                   \n\t"
        "pxor       %[ftmp0],   %[ftmp0],    %[ftmp0]      \n\t"
        MMI_ULDC1(%[filter1], %[filter], 0x00)
        MMI_ULDC1(%[filter2], %[filter], 0x08)
        "li         %[tmp0],    0x07                       \n\t"
        "dmtc1      %[tmp0],    %[ftmp13]                  \n\t"
        "punpcklwd  %[ftmp13],  %[ftmp13],   %[ftmp13]     \n\t"
        "1:                                                \n\t"
        /* Get 8 data per row */
        MMI_ULDC1(%[ftmp5], %[src], 0x00)
        MMI_ULDC1(%[ftmp7], %[src], 0x01)
        MMI_ULDC1(%[ftmp9], %[src], 0x02)
        MMI_ULDC1(%[ftmp11], %[src], 0x03)
        "punpcklbh  %[ftmp4],   %[ftmp5],    %[ftmp0]      \n\t"
        "punpckhbh  %[ftmp5],   %[ftmp5],    %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp7],    %[ftmp0]      \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp7],    %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp8],   %[ftmp9],    %[ftmp0]      \n\t"
        "punpckhbh  %[ftmp9],   %[ftmp9],    %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp10],  %[ftmp11],   %[ftmp0]      \n\t"
        "punpckhbh  %[ftmp11],  %[ftmp11],   %[ftmp0]      \n\t"
        PTR_ADDIU  "%[width],   %[width],    -0x04         \n\t"
        /* Get raw data */
        GET_DATA_H_MMI
        ROUND_POWER_OF_TWO_MMI(%[srcl], %[ftmp13], %[ftmp5],
                               %[ftmp6], %[tmp0])
        ROUND_POWER_OF_TWO_MMI(%[srch], %[ftmp13], %[ftmp5],
                               %[ftmp6], %[tmp0])
        "packsswh   %[srcl],    %[srcl],     %[srch]       \n\t"
        "packushb   %[ftmp12],  %[srcl],     %[ftmp0]      \n\t"
        "swc1       %[ftmp12],  0x00(%[dst])               \n\t"
        PTR_ADDIU  "%[dst],     %[dst],      0x04          \n\t"
        PTR_ADDIU  "%[src],     %[src],      0x04          \n\t"
        /* Loop count */
        "bnez       %[width],   1b                         \n\t"
        "move       %[width],   %[tmp1]                    \n\t"
        PTR_ADDU   "%[src],     %[src],      %[src_stride] \n\t"
        PTR_ADDU   "%[dst],     %[dst],      %[dst_stride] \n\t"
        PTR_ADDIU  "%[height],  %[height],   -0x01         \n\t"
        "bnez       %[height],  1b                         \n\t"
        : RESTRICT_ASM_ALL64
          [srcl]"=&f"(ftmp[0]),     [srch]"=&f"(ftmp[1]),
          [filter1]"=&f"(ftmp[2]),  [filter2]"=&f"(ftmp[3]),
          [ftmp0]"=&f"(ftmp[4]),    [ftmp4]"=&f"(ftmp[5]),
          [ftmp5]"=&f"(ftmp[6]),    [ftmp6]"=&f"(ftmp[7]),
          [ftmp7]"=&f"(ftmp[8]),    [ftmp8]"=&f"(ftmp[9]),
          [ftmp9]"=&f"(ftmp[10]),   [ftmp10]"=&f"(ftmp[11]),
          [ftmp11]"=&f"(ftmp[12]),  [ftmp12]"=&f"(ftmp[13]),
          [tmp0]"=&r"(tmp[0]),      [tmp1]"=&r"(tmp[1]),
          [src]"+&r"(src),          [width]"+&r"(w),
          [dst]"+&r"(dst),          [height]"+&r"(h),
          [ftmp13]"=&f"(ftmp[14])
        : [filter]"r"(filter_x),
          [src_stride]"r"((mips_reg)src_stride),
          [dst_stride]"r"((mips_reg)dst_stride)
        : "memory"
    );
}

static void convolve_vert_mmi(const uint8_t *src, int32_t src_stride,
                              uint8_t *dst, int32_t dst_stride,
                              const int16_t *filter_y, int32_t w,
                              int32_t h)
{
    double ftmp[17];
    uint32_t tmp[1];
    ptrdiff_t addr = src_stride;
    DECLARE_VAR_ALL64;
    src_stride -= w;
    dst_stride -= w;

    __asm__ volatile (
        "pxor       %[ftmp0],    %[ftmp0],   %[ftmp0]      \n\t"
        MMI_ULDC1(%[ftmp4], %[filter], 0x00)
        MMI_ULDC1(%[ftmp5], %[filter], 0x08)
        "punpcklwd  %[filter10], %[ftmp4],   %[ftmp4]      \n\t"
        "punpckhwd  %[filter32], %[ftmp4],   %[ftmp4]      \n\t"
        "punpcklwd  %[filter54], %[ftmp5],   %[ftmp5]      \n\t"
        "punpckhwd  %[filter76], %[ftmp5],   %[ftmp5]      \n\t"
        "li         %[tmp0],     0x07                      \n\t"
        "dmtc1      %[tmp0],     %[ftmp13]                 \n\t"
        "punpcklwd  %[ftmp13],   %[ftmp13],  %[ftmp13]     \n\t"
        "1:                                                \n\t"
        /* Get 8 data per column */
        MMI_ULDC1(%[ftmp4], %[src], 0x0)
        PTR_ADDU   "%[tmp0],     %[src],     %[addr]       \n\t"
        MMI_ULDC1(%[ftmp5], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp6], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp7], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp8], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp9], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp10], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp11], %[tmp0], 0x0)
        "punpcklbh  %[ftmp4],    %[ftmp4],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp5],    %[ftmp5],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp6],    %[ftmp6],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp7],    %[ftmp7],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp8],    %[ftmp8],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp9],    %[ftmp9],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp10],   %[ftmp10],  %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp11],   %[ftmp11],  %[ftmp0]      \n\t"
        PTR_ADDIU  "%[width],    %[width],   -0x04         \n\t"
        /* Get raw data */
        GET_DATA_V_MMI
        ROUND_POWER_OF_TWO_MMI(%[srcl], %[ftmp13], %[ftmp5],
                               %[ftmp6], %[tmp0])
        ROUND_POWER_OF_TWO_MMI(%[srch], %[ftmp13], %[ftmp5],
                               %[ftmp6], %[tmp0])
        "packsswh   %[srcl],     %[srcl],    %[srch]       \n\t"
        "packushb   %[ftmp12],   %[srcl],    %[ftmp0]      \n\t"
        "swc1       %[ftmp12],   0x00(%[dst])              \n\t"
        PTR_ADDIU  "%[dst],      %[dst],      0x04         \n\t"
        PTR_ADDIU  "%[src],      %[src],      0x04         \n\t"
        /* Loop count */
        "bnez       %[width],    1b                        \n\t"
        PTR_SUBU   "%[width],    %[addr],    %[src_stride] \n\t"
        PTR_ADDU   "%[src],      %[src],     %[src_stride] \n\t"
        PTR_ADDU   "%[dst],      %[dst],     %[dst_stride] \n\t"
        PTR_ADDIU  "%[height],   %[height],  -0x01         \n\t"
        "bnez       %[height],   1b                        \n\t"
        : RESTRICT_ASM_ALL64
          [srcl]"=&f"(ftmp[0]),     [srch]"=&f"(ftmp[1]),
          [filter10]"=&f"(ftmp[2]), [filter32]"=&f"(ftmp[3]),
          [filter54]"=&f"(ftmp[4]), [filter76]"=&f"(ftmp[5]),
          [ftmp0]"=&f"(ftmp[6]),    [ftmp4]"=&f"(ftmp[7]),
          [ftmp5]"=&f"(ftmp[8]),    [ftmp6]"=&f"(ftmp[9]),
          [ftmp7]"=&f"(ftmp[10]),   [ftmp8]"=&f"(ftmp[11]),
          [ftmp9]"=&f"(ftmp[12]),   [ftmp10]"=&f"(ftmp[13]),
          [ftmp11]"=&f"(ftmp[14]),  [ftmp12]"=&f"(ftmp[15]),
          [src]"+&r"(src),          [dst]"+&r"(dst),
          [width]"+&r"(w),          [height]"+&r"(h),
          [tmp0]"=&r"(tmp[0]),      [ftmp13]"=&f"(ftmp[16])
        : [filter]"r"(filter_y),
          [src_stride]"r"((mips_reg)src_stride),
          [dst_stride]"r"((mips_reg)dst_stride),
          [addr]"r"((mips_reg)addr)
        : "memory"
    );
}

static void convolve_avg_horiz_mmi(const uint8_t *src, int32_t src_stride,
                                   uint8_t *dst, int32_t dst_stride,
                                   const uint16_t *filter_x, int32_t w,
                                   int32_t h)
{
    double ftmp[15];
    uint32_t tmp[2];
    DECLARE_VAR_ALL64;
    src -= 3;
    src_stride -= w;
    dst_stride -= w;

    __asm__ volatile (
        "move       %[tmp1],    %[width]                   \n\t"
        "pxor       %[ftmp0],   %[ftmp0],    %[ftmp0]      \n\t"
        MMI_ULDC1(%[filter1], %[filter], 0x00)
        MMI_ULDC1(%[filter2], %[filter], 0x08)
        "li         %[tmp0],    0x07                       \n\t"
        "dmtc1      %[tmp0],    %[ftmp13]                  \n\t"
        "punpcklwd  %[ftmp13],  %[ftmp13],   %[ftmp13]     \n\t"
        "1:                                                \n\t"
        /* Get 8 data per row */
        MMI_ULDC1(%[ftmp5], %[src], 0x00)
        MMI_ULDC1(%[ftmp7], %[src], 0x01)
        MMI_ULDC1(%[ftmp9], %[src], 0x02)
        MMI_ULDC1(%[ftmp11], %[src], 0x03)
        "punpcklbh  %[ftmp4],   %[ftmp5],    %[ftmp0]      \n\t"
        "punpckhbh  %[ftmp5],   %[ftmp5],    %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp7],    %[ftmp0]      \n\t"
        "punpckhbh  %[ftmp7],   %[ftmp7],    %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp8],   %[ftmp9],    %[ftmp0]      \n\t"
        "punpckhbh  %[ftmp9],   %[ftmp9],    %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp10],  %[ftmp11],   %[ftmp0]      \n\t"
        "punpckhbh  %[ftmp11],  %[ftmp11],   %[ftmp0]      \n\t"
        PTR_ADDIU  "%[width],   %[width],    -0x04         \n\t"
        /* Get raw data */
        GET_DATA_H_MMI
        ROUND_POWER_OF_TWO_MMI(%[srcl], %[ftmp13], %[ftmp5],
                               %[ftmp6], %[tmp0])
        ROUND_POWER_OF_TWO_MMI(%[srch], %[ftmp13], %[ftmp5],
                               %[ftmp6], %[tmp0])
        "packsswh   %[srcl],    %[srcl],     %[srch]       \n\t"
        "packushb   %[ftmp12],  %[srcl],     %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp12],  %[ftmp12],   %[ftmp0]      \n\t"
        MMI_ULDC1(%[ftmp4], %[dst], 0x0)
        "punpcklbh  %[ftmp4],   %[ftmp4],    %[ftmp0]      \n\t"
        "paddh      %[ftmp12],  %[ftmp12],   %[ftmp4]      \n\t"
        "li         %[tmp0],    0x10001                    \n\t"
        "dmtc1      %[tmp0],    %[ftmp5]                   \n\t"
        "punpcklhw  %[ftmp5],   %[ftmp5],    %[ftmp5]      \n\t"
        "paddh      %[ftmp12],  %[ftmp12],   %[ftmp5]      \n\t"
        "psrah      %[ftmp12],  %[ftmp12],   %[ftmp5]      \n\t"
        "packushb   %[ftmp12],  %[ftmp12],   %[ftmp0]      \n\t"
        "swc1       %[ftmp12],  0x00(%[dst])               \n\t"
        PTR_ADDIU  "%[dst],     %[dst],      0x04          \n\t"
        PTR_ADDIU  "%[src],     %[src],      0x04          \n\t"
        /* Loop count */
        "bnez       %[width],   1b                         \n\t"
        "move       %[width],   %[tmp1]                    \n\t"
        PTR_ADDU   "%[src],     %[src],      %[src_stride] \n\t"
        PTR_ADDU   "%[dst],     %[dst],      %[dst_stride] \n\t"
        PTR_ADDIU  "%[height],  %[height],   -0x01         \n\t"
        "bnez       %[height],  1b                         \n\t"
        : RESTRICT_ASM_ALL64
          [srcl]"=&f"(ftmp[0]),     [srch]"=&f"(ftmp[1]),
          [filter1]"=&f"(ftmp[2]),  [filter2]"=&f"(ftmp[3]),
          [ftmp0]"=&f"(ftmp[4]),    [ftmp4]"=&f"(ftmp[5]),
          [ftmp5]"=&f"(ftmp[6]),    [ftmp6]"=&f"(ftmp[7]),
          [ftmp7]"=&f"(ftmp[8]),    [ftmp8]"=&f"(ftmp[9]),
          [ftmp9]"=&f"(ftmp[10]),   [ftmp10]"=&f"(ftmp[11]),
          [ftmp11]"=&f"(ftmp[12]),  [ftmp12]"=&f"(ftmp[13]),
          [tmp0]"=&r"(tmp[0]),      [tmp1]"=&r"(tmp[1]),
          [src]"+&r"(src),          [width]"+&r"(w),
          [dst]"+&r"(dst),          [height]"+&r"(h),
          [ftmp13]"=&f"(ftmp[14])
        : [filter]"r"(filter_x),
          [src_stride]"r"((mips_reg)src_stride),
          [dst_stride]"r"((mips_reg)dst_stride)
        : "memory"
    );
}

static void convolve_avg_vert_mmi(const uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  const int16_t *filter_y, int32_t w,
                                  int32_t h)
{
    double ftmp[17];
    uint32_t tmp[1];
    ptrdiff_t addr = src_stride;
    DECLARE_VAR_ALL64;
    src_stride -= w;
    dst_stride -= w;

    __asm__ volatile (
        "pxor       %[ftmp0],    %[ftmp0],   %[ftmp0]      \n\t"
        MMI_ULDC1(%[ftmp4], %[filter], 0x00)
        MMI_ULDC1(%[ftmp5], %[filter], 0x08)
        "punpcklwd  %[filter10], %[ftmp4],   %[ftmp4]      \n\t"
        "punpckhwd  %[filter32], %[ftmp4],   %[ftmp4]      \n\t"
        "punpcklwd  %[filter54], %[ftmp5],   %[ftmp5]      \n\t"
        "punpckhwd  %[filter76], %[ftmp5],   %[ftmp5]      \n\t"
        "li         %[tmp0],     0x07                      \n\t"
        "dmtc1      %[tmp0],     %[ftmp13]                 \n\t"
        "punpcklwd  %[ftmp13],   %[ftmp13],  %[ftmp13]     \n\t"
        "1:                                                \n\t"
        /* Get 8 data per column */
        MMI_ULDC1(%[ftmp4], %[src], 0x0)
        PTR_ADDU   "%[tmp0],     %[src],     %[addr]       \n\t"
        MMI_ULDC1(%[ftmp5], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp6], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp7], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp8], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp9], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp10], %[tmp0], 0x0)
        PTR_ADDU   "%[tmp0],     %[tmp0],    %[addr]       \n\t"
        MMI_ULDC1(%[ftmp11], %[tmp0], 0x0)
        "punpcklbh  %[ftmp4],    %[ftmp4],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp5],    %[ftmp5],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp6],    %[ftmp6],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp7],    %[ftmp7],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp8],    %[ftmp8],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp9],    %[ftmp9],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp10],   %[ftmp10],  %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp11],   %[ftmp11],  %[ftmp0]      \n\t"
        PTR_ADDIU  "%[width],    %[width],   -0x04         \n\t"
        /* Get raw data */
        GET_DATA_V_MMI
        ROUND_POWER_OF_TWO_MMI(%[srcl], %[ftmp13], %[ftmp5],
                               %[ftmp6], %[tmp0])
        ROUND_POWER_OF_TWO_MMI(%[srch], %[ftmp13], %[ftmp5],
                               %[ftmp6], %[tmp0])
        "packsswh   %[srcl],     %[srcl],    %[srch]       \n\t"
        "packushb   %[ftmp12],   %[srcl],    %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp12],   %[ftmp12],  %[ftmp0]      \n\t"
        MMI_ULDC1(%[ftmp4], %[dst], 0x00)
        "punpcklbh  %[ftmp4],    %[ftmp4],   %[ftmp0]      \n\t"
        "paddh      %[ftmp12],   %[ftmp12],  %[ftmp4]      \n\t"
        "li         %[tmp0],     0x10001                   \n\t"
        "dmtc1      %[tmp0],     %[ftmp5]                  \n\t"
        "punpcklhw  %[ftmp5],    %[ftmp5],   %[ftmp5]      \n\t"
        "paddh      %[ftmp12],   %[ftmp12],  %[ftmp5]      \n\t"
        "psrah      %[ftmp12],   %[ftmp12],  %[ftmp5]      \n\t"
        "packushb   %[ftmp12],   %[ftmp12],  %[ftmp0]      \n\t"
        "swc1       %[ftmp12],   0x00(%[dst])              \n\t"
        PTR_ADDIU  "%[dst],      %[dst],     0x04          \n\t"
        PTR_ADDIU  "%[src],      %[src],     0x04          \n\t"
        /* Loop count */
        "bnez       %[width],    1b                        \n\t"
        PTR_SUBU   "%[width],    %[addr],    %[src_stride] \n\t"
        PTR_ADDU   "%[src],      %[src],     %[src_stride] \n\t"
        PTR_ADDU   "%[dst],      %[dst],     %[dst_stride] \n\t"
        PTR_ADDIU  "%[height],   %[height],  -0x01         \n\t"
        "bnez       %[height],   1b                        \n\t"
        : RESTRICT_ASM_ALL64
          [srcl]"=&f"(ftmp[0]),     [srch]"=&f"(ftmp[1]),
          [filter10]"=&f"(ftmp[2]), [filter32]"=&f"(ftmp[3]),
          [filter54]"=&f"(ftmp[4]), [filter76]"=&f"(ftmp[5]),
          [ftmp0]"=&f"(ftmp[6]),    [ftmp4]"=&f"(ftmp[7]),
          [ftmp5]"=&f"(ftmp[8]),    [ftmp6]"=&f"(ftmp[9]),
          [ftmp7]"=&f"(ftmp[10]),   [ftmp8]"=&f"(ftmp[11]),
          [ftmp9]"=&f"(ftmp[12]),   [ftmp10]"=&f"(ftmp[13]),
          [ftmp11]"=&f"(ftmp[14]),  [ftmp12]"=&f"(ftmp[15]),
          [src]"+&r"(src),          [dst]"+&r"(dst),
          [width]"+&r"(w),          [height]"+&r"(h),
          [tmp0]"=&r"(tmp[0]),      [ftmp13]"=&f"(ftmp[16])
        : [filter]"r"(filter_y),
          [src_stride]"r"((mips_reg)src_stride),
          [dst_stride]"r"((mips_reg)dst_stride),
          [addr]"r"((mips_reg)addr)
        : "memory"
    );
}

static void convolve_avg_mmi(const uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t w, int32_t h)
{
    double ftmp[4];
    uint32_t tmp[2];
    DECLARE_VAR_ALL64;
    src_stride -= w;
    dst_stride -= w;

    __asm__ volatile (
        "move       %[tmp1],    %[width]                  \n\t"
        "pxor       %[ftmp0],   %[ftmp0],   %[ftmp0]      \n\t"
        "li         %[tmp0],    0x10001                   \n\t"
        "dmtc1      %[tmp0],    %[ftmp3]                  \n\t"
        "punpcklhw  %[ftmp3],   %[ftmp3],   %[ftmp3]      \n\t"
        "1:                                               \n\t"
        MMI_ULDC1(%[ftmp1], %[src], 0x00)
        MMI_ULDC1(%[ftmp2], %[dst], 0x00)
        "punpcklbh  %[ftmp1],   %[ftmp1],   %[ftmp0]      \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],   %[ftmp0]      \n\t"
        "paddh      %[ftmp1],   %[ftmp1],   %[ftmp2]      \n\t"
        "paddh      %[ftmp1],   %[ftmp1],   %[ftmp3]      \n\t"
        "psrah      %[ftmp1],   %[ftmp1],   %[ftmp3]      \n\t"
        "packushb   %[ftmp1],   %[ftmp1],   %[ftmp0]      \n\t"
        "swc1       %[ftmp1],   0x00(%[dst])              \n\t"
        PTR_ADDIU  "%[width],   %[width],   -0x04         \n\t"
        PTR_ADDIU  "%[dst],     %[dst],     0x04          \n\t"
        PTR_ADDIU  "%[src],     %[src],     0x04          \n\t"
        "bnez       %[width],   1b                        \n\t"
        "move       %[width],   %[tmp1]                   \n\t"
        PTR_ADDU   "%[dst],     %[dst],     %[dst_stride] \n\t"
        PTR_ADDU   "%[src],     %[src],     %[src_stride] \n\t"
        PTR_ADDIU  "%[height],  %[height],  -0x01         \n\t"
        "bnez       %[height],  1b                        \n\t"
        : RESTRICT_ASM_ALL64
          [ftmp0]"=&f"(ftmp[0]),  [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),  [ftmp3]"=&f"(ftmp[3]),
          [tmp0]"=&r"(tmp[0]),    [tmp1]"=&r"(tmp[1]),
          [src]"+&r"(src),        [dst]"+&r"(dst),
          [width]"+&r"(w),        [height]"+&r"(h)
        : [src_stride]"r"((mips_reg)src_stride),
          [dst_stride]"r"((mips_reg)dst_stride)
        : "memory"
    );
}

static const int16_t vp9_subpel_filters_mmi[3][15][8] = {
    [FILTER_8TAP_REGULAR] = {
         {0, 1, -5, 126, 8, -3, 1, 0},
         {-1, 3, -10, 122, 18, -6, 2, 0},
         {-1, 4, -13, 118, 27, -9, 3, -1},
         {-1, 4, -16, 112, 37, -11, 4, -1},
         {-1, 5, -18, 105, 48, -14, 4, -1},
         {-1, 5, -19, 97, 58, -16, 5, -1},
         {-1, 6, -19, 88, 68, -18, 5, -1},
         {-1, 6, -19, 78, 78, -19, 6, -1},
         {-1, 5, -18, 68, 88, -19, 6, -1},
         {-1, 5, -16, 58, 97, -19, 5, -1},
         {-1, 4, -14, 48, 105, -18, 5, -1},
         {-1, 4, -11, 37, 112, -16, 4, -1},
         {-1, 3, -9, 27, 118, -13, 4, -1},
         {0, 2, -6, 18, 122, -10, 3, -1},
         {0, 1, -3, 8, 126, -5, 1, 0},
    }, [FILTER_8TAP_SHARP] = {
        {-1, 3, -7, 127, 8, -3, 1, 0},
        {-2, 5, -13, 125, 17, -6, 3, -1},
        {-3, 7, -17, 121, 27, -10, 5, -2},
        {-4, 9, -20, 115, 37, -13, 6, -2},
        {-4, 10, -23, 108, 48, -16, 8, -3},
        {-4, 10, -24, 100, 59, -19, 9, -3},
        {-4, 11, -24, 90, 70, -21, 10, -4},
        {-4, 11, -23, 80, 80, -23, 11, -4},
        {-4, 10, -21, 70, 90, -24, 11, -4},
        {-3, 9, -19, 59, 100, -24, 10, -4},
        {-3, 8, -16, 48, 108, -23, 10, -4},
        {-2, 6, -13, 37, 115, -20, 9, -4},
        {-2, 5, -10, 27, 121, -17, 7, -3},
        {-1, 3, -6, 17, 125, -13, 5, -2},
        {0, 1, -3, 8, 127, -7, 3, -1},
    }, [FILTER_8TAP_SMOOTH] = {
        {-3, -1, 32, 64, 38, 1, -3, 0},
        {-2, -2, 29, 63, 41, 2, -3, 0},
        {-2, -2, 26, 63, 43, 4, -4, 0},
        {-2, -3, 24, 62, 46, 5, -4, 0},
        {-2, -3, 21, 60, 49, 7, -4, 0},
        {-1, -4, 18, 59, 51, 9, -4, 0},
        {-1, -4, 16, 57, 53, 12, -4, -1},
        {-1, -4, 14, 55, 55, 14, -4, -1},
        {-1, -4, 12, 53, 57, 16, -4, -1},
        {0, -4, 9, 51, 59, 18, -4, -1},
        {0, -4, 7, 49, 60, 21, -3, -2},
        {0, -4, 5, 46, 62, 24, -3, -2},
        {0, -4, 4, 43, 63, 26, -2, -2},
        {0, -3, 2, 41, 63, 29, -2, -2},
        {0, -3, 1, 38, 64, 32, -1, -3},
    }
};

#define VP9_8TAP_MIPS_MMI_FUNC(SIZE, TYPE, TYPE_IDX)                           \
void ff_put_8tap_##TYPE##_##SIZE##h_mmi(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int16_t *filter = vp9_subpel_filters_mmi[TYPE_IDX][mx-1];            \
                                                                               \
    convolve_horiz_mmi(src, srcstride, dst, dststride, filter, SIZE, h);       \
}                                                                              \
                                                                               \
void ff_put_8tap_##TYPE##_##SIZE##v_mmi(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int16_t *filter = vp9_subpel_filters_mmi[TYPE_IDX][my-1];            \
                                                                               \
    src -= (3 * srcstride);                                                    \
    convolve_vert_mmi(src, srcstride, dst, dststride, filter, SIZE, h);        \
}                                                                              \
                                                                               \
void ff_put_8tap_##TYPE##_##SIZE##hv_mmi(uint8_t *dst, ptrdiff_t dststride,    \
                                         const uint8_t *src,                   \
                                         ptrdiff_t srcstride,                  \
                                         int h, int mx, int my)                \
{                                                                              \
    const uint16_t *hfilter = vp9_subpel_filters_mmi[TYPE_IDX][mx-1];          \
    const uint16_t *vfilter = vp9_subpel_filters_mmi[TYPE_IDX][my-1];          \
                                                                               \
    int tmp_h = h + 7;                                                         \
    uint8_t temp[64 * 71];                                                     \
    src -= (3 * srcstride);                                                    \
    convolve_horiz_mmi(src, srcstride, temp, 64, hfilter, SIZE, tmp_h);        \
    convolve_vert_mmi(temp, 64, dst, dststride, vfilter, SIZE, h);             \
}                                                                              \
                                                                               \
void ff_avg_8tap_##TYPE##_##SIZE##h_mmi(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int16_t *filter = vp9_subpel_filters_mmi[TYPE_IDX][mx-1];            \
                                                                               \
    convolve_avg_horiz_mmi(src, srcstride, dst, dststride, filter, SIZE, h);   \
}                                                                              \
                                                                               \
void ff_avg_8tap_##TYPE##_##SIZE##v_mmi(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int16_t *filter = vp9_subpel_filters_mmi[TYPE_IDX][my-1];            \
                                                                               \
    src -= (3 * srcstride);                                                    \
    convolve_avg_vert_mmi(src, srcstride, dst, dststride, filter, SIZE, h);    \
}                                                                              \
                                                                               \
void ff_avg_8tap_##TYPE##_##SIZE##hv_mmi(uint8_t *dst, ptrdiff_t dststride,    \
                                         const uint8_t *src,                   \
                                         ptrdiff_t srcstride,                  \
                                         int h, int mx, int my)                \
{                                                                              \
    const uint16_t *hfilter = vp9_subpel_filters_mmi[TYPE_IDX][mx-1];          \
    const uint16_t *vfilter = vp9_subpel_filters_mmi[TYPE_IDX][my-1];          \
                                                                               \
    uint8_t temp1[64 * 64];                                                    \
    uint8_t temp2[64 * 71];                                                    \
    int tmp_h = h + 7;                                                         \
    src -= (3 * srcstride);                                                    \
    convolve_horiz_mmi(src, srcstride, temp2, 64, hfilter, SIZE, tmp_h);       \
    convolve_vert_mmi(temp2, 64, temp1, 64, vfilter, SIZE, h);                 \
    convolve_avg_mmi(temp1, 64, dst, dststride, SIZE, h);                      \
}

VP9_8TAP_MIPS_MMI_FUNC(64, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MMI_FUNC(32, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MMI_FUNC(16, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MMI_FUNC(8, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_MIPS_MMI_FUNC(4, regular, FILTER_8TAP_REGULAR);

VP9_8TAP_MIPS_MMI_FUNC(64, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MMI_FUNC(32, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MMI_FUNC(16, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MMI_FUNC(8, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_MIPS_MMI_FUNC(4, sharp, FILTER_8TAP_SHARP);

VP9_8TAP_MIPS_MMI_FUNC(64, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MMI_FUNC(32, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MMI_FUNC(16, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MMI_FUNC(8, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_MIPS_MMI_FUNC(4, smooth, FILTER_8TAP_SMOOTH);

#undef VP9_8TAP_MIPS_MMI_FUNC
