/*
 * Loongson SIMD optimized h264dsp
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

#include "libavcodec/bit_depth_template.c"
#include "h264dsp_mips.h"

void ff_h264_weight_pixels16_8_mmi(uint8_t *block, int stride,
        int height, int log2_denom, int weight, int offset)
{
    int y;

    offset <<= log2_denom;

    if (log2_denom)
        offset += 1 << (log2_denom - 1);

    for (y=0; y<height; y++, block+=stride) {
        __asm__ volatile (
            "ldc1 $f2, %0                   \r\n"
            "ldc1 $f4, %1                   \r\n"
            "dmtc1 $0, $f20                 \r\n"
            "mtc1 %2, $f6                   \r\n"
            "mtc1 %3, $f8                   \r\n"
            "mtc1 %4, $f10                  \r\n"
            "pshufh $f6, $f6, $f20          \r\n"
            "pshufh $f8, $f8, $f20          \r\n"
            "punpckhbh $f14, $f2, $f20      \r\n"
            "punpckhbh $f16, $f4, $f20      \r\n"
            "punpcklbh $f2, $f2, $f20       \r\n"
            "punpcklbh $f4, $f4, $f20       \r\n"
            "pmullh $f14, $f14, $f6         \r\n"
            "pmullh $f16, $f16, $f6         \r\n"
            "pmullh $f2, $f2, $f6           \r\n"
            "pmullh $f4, $f4, $f6           \r\n"
            "paddsh $f14, $f14, $f8         \r\n"
            "paddsh $f16, $f16, $f8         \r\n"
            "paddsh $f2, $f2, $f8           \r\n"
            "paddsh $f4, $f4, $f8           \r\n"
            "psrah $f14, $f14, $f10         \r\n"
            "psrah $f16, $f16, $f10         \r\n"
            "psrah $f2, $f2, $f10           \r\n"
            "psrah $f4, $f4, $f10           \r\n"
            "packushb $f2, $f2, $f14        \r\n"
            "packushb $f4, $f4, $f16        \r\n"
            "sdc1 $f2, %0                   \r\n"
            "sdc1 $f4, %1                   \r\n"
            : "=m"(*block),"=m"(*(block + 8))
            : "r"(weight),"r"(offset),"r"(log2_denom)
        );
    }
}

void ff_h264_biweight_pixels16_8_mmi(uint8_t *dst, uint8_t *src,
        int stride, int height, int log2_denom, int weightd, int weights,
        int offset)
{
    int y;

    offset = ((offset + 1) | 1) << log2_denom;

    for (y=0; y<height; y++, dst+=stride, src+=stride) {
        __asm__ volatile (
            "ldc1 $f2, %2                   \r\n"
            "ldc1 $f4, %3                   \r\n"
            "dmtc1 $0, $f20                 \r\n"
            "mtc1 %6, $f6                   \r\n"
            "mtc1 %7, $f8                   \r\n"
            "mtc1 %8, $f10                  \r\n"
            "mtc1 %9, $f12                  \r\n"
            "pshufh $f6, $f6, $f20          \r\n"
            "pshufh $f8, $f8, $f20          \r\n"
            "pshufh $f10, $f10, $f20        \r\n"
            "punpckhbh $f14, $f2, $f20      \r\n"
            "punpckhbh $f16, $f4, $f20      \r\n"
            "punpcklbh $f2, $f2, $f20       \r\n"
            "punpcklbh $f4, $f4, $f20       \r\n"
            "pmullh $f14, $f14, $f6         \r\n"
            "pmullh $f16, $f16, $f8         \r\n"
            "pmullh $f2, $f2, $f6           \r\n"
            "pmullh $f4, $f4, $f8           \r\n"
            "paddsh $f14, $f14, $f10        \r\n"
            "paddsh $f2, $f2, $f10          \r\n"
            "paddsh $f14, $f14, $f16        \r\n"
            "paddsh $f2, $f2, $f4           \r\n"
            "psrah $f14, $f14, $f12         \r\n"
            "psrah $f2, $f2, $f12           \r\n"
            "packushb $f2, $f2, $f14        \r\n"
            "sdc1 $f2, %0                   \r\n"
            "ldc1 $f2, %4                   \r\n"
            "ldc1 $f4, %5                   \r\n"
            "punpckhbh $f14, $f2, $f20      \r\n"
            "punpckhbh $f16, $f4, $f20      \r\n"
            "punpcklbh $f2, $f2, $f20       \r\n"
            "punpcklbh $f4, $f4, $f20       \r\n"
            "pmullh $f14, $f14, $f6         \r\n"
            "pmullh $f16, $f16, $f8         \r\n"
            "pmullh $f2, $f2, $f6           \r\n"
            "pmullh $f4, $f4, $f8           \r\n"
            "paddsh $f14, $f14, $f10        \r\n"
            "paddsh $f2, $f2, $f10          \r\n"
            "paddsh $f14, $f14, $f16        \r\n"
            "paddsh $f2, $f2, $f4           \r\n"
            "psrah $f14, $f14, $f12         \r\n"
            "psrah $f2, $f2, $f12           \r\n"
            "packushb $f2, $f2, $f14        \r\n"
            "sdc1 $f2, %1                   \r\n"
            : "=m"(*dst),"=m"(*(dst+8))
            : "m"(*src),"m"(*dst),"m"(*(src+8)),"m"(*(dst+8)),
              "r"(weights),"r"(weightd),"r"(offset),"r"(log2_denom+1)
        );
    }
}

void ff_h264_weight_pixels8_8_mmi(uint8_t *block, int stride, int height,
        int log2_denom, int weight, int offset)
{
    int y;

    offset <<= log2_denom;

    if (log2_denom)
        offset += 1 << (log2_denom - 1);

    for (y=0; y<height; y++, block+=stride) {
        __asm__ volatile (
            "ldc1 $f2, %0                   \r\n"
            "mtc1 %1, $f6                   \r\n"
            "mtc1 %2, $f8                   \r\n"
            "mtc1 %3, $f10                  \r\n"
            "dmtc1 $0, $f20                 \r\n"
            "pshufh $f6, $f6, $f20          \r\n"
            "pshufh $f8, $f8, $f20          \r\n"
            "punpckhbh $f14, $f2, $f20      \r\n"
            "punpcklbh $f2, $f2, $f20       \r\n"
            "pmullh $f14, $f14, $f6         \r\n"
            "pmullh $f2, $f2, $f6           \r\n"
            "paddsh $f14, $f14, $f8         \r\n"
            "paddsh $f2, $f2, $f8           \r\n"
            "psrah $f14, $f14, $f10         \r\n"
            "psrah $f2, $f2, $f10           \r\n"
            "packushb $f2, $f2, $f14        \r\n"
            "sdc1 $f2, %0                   \r\n"
            : "=m"(*block)
            : "r"(weight),"r"(offset),"r"(log2_denom)
        );
    }
}

void ff_h264_biweight_pixels8_8_mmi(uint8_t *dst, uint8_t *src,
        int stride, int height, int log2_denom, int weightd, int weights,
        int offset)
{
    int y;

    offset = ((offset + 1) | 1) << log2_denom;

    for (y=0; y<height; y++, dst+=stride, src+=stride) {
        __asm__ volatile (
            "ldc1 $f2, %1                   \r\n"
            "ldc1 $f4, %2                   \r\n"
            "dmtc1 $0, $f20                 \r\n"
            "mtc1 %3, $f6                   \r\n"
            "mtc1 %4, $f8                   \r\n"
            "mtc1 %5, $f10                  \r\n"
            "mtc1 %6, $f12                  \r\n"
            "pshufh $f6, $f6, $f20          \r\n"
            "pshufh $f8, $f8, $f20          \r\n"
            "pshufh $f10, $f10, $f20        \r\n"
            "punpckhbh $f14, $f2, $f20      \r\n"
            "punpckhbh $f16, $f4, $f20      \r\n"
            "punpcklbh $f2, $f2, $f20       \r\n"
            "punpcklbh $f4, $f4, $f20       \r\n"
            "pmullh $f14, $f14, $f6         \r\n"
            "pmullh $f16, $f16, $f8         \r\n"
            "pmullh $f2, $f2, $f6           \r\n"
            "pmullh $f4, $f4, $f8           \r\n"
            "paddsh $f14, $f14, $f10        \r\n"
            "paddsh $f2, $f2, $f10          \r\n"
            "paddsh $f14, $f14, $f16        \r\n"
            "paddsh $f2, $f2, $f4           \r\n"
            "psrah $f14, $f14, $f12         \r\n"
            "psrah $f2, $f2, $f12           \r\n"
            "packushb $f2, $f2, $f14        \r\n"
            "sdc1 $f2, %0                   \r\n"
            : "=m"(*dst)
            : "m"(*src),"m"(*dst),"r"(weights),
              "r"(weightd),"r"(offset),"r"(log2_denom+1)
        );
    }
}

void ff_h264_weight_pixels4_8_mmi(uint8_t *block, int stride, int height,
        int log2_denom, int weight, int offset)
{
    int y;

    offset <<= log2_denom;

    if (log2_denom)
        offset += 1 << (log2_denom - 1);

    for (y=0; y<height; y++, block+=stride) {
        __asm__ volatile (
            "lwc1 $f2, %0                   \r\n"
            "mtc1 %1, $f6                   \r\n"
            "mtc1 %2, $f8                   \r\n"
            "mtc1 %3, $f10                  \r\n"
            "dmtc1 $0, $f20                 \r\n"
            "pshufh $f6, $f6, $f20          \r\n"
            "pshufh $f8, $f8, $f20          \r\n"
            "punpcklbh $f2, $f2, $f20       \r\n"
            "pmullh $f2, $f2, $f6           \r\n"
            "paddsh $f2, $f2, $f8           \r\n"
            "psrah $f2, $f2, $f10           \r\n"
            "packushb $f2, $f2, $f20        \r\n"
            "swc1 $f2, %0                   \r\n"
            : "=m"(*block)
            : "r"(weight),"r"(offset),"r"(log2_denom)
        );
    }
}

void ff_h264_biweight_pixels4_8_mmi(uint8_t *dst, uint8_t *src,
        int stride, int height, int log2_denom, int weightd, int weights,
        int offset)
{
    int y;

    offset = ((offset + 1) | 1) << log2_denom;

    for (y=0; y<height; y++, dst+=stride, src+=stride) {
        __asm__ volatile (
            "lwc1 $f2, %1                   \r\n"
            "lwc1 $f4, %2                   \r\n"
            "dmtc1 $0, $f20                 \r\n"
            "mtc1 %3, $f6                   \r\n"
            "mtc1 %4, $f8                   \r\n"
            "mtc1 %5, $f10                  \r\n"
            "mtc1 %6, $f12                  \r\n"
            "pshufh $f6, $f6, $f20          \r\n"
            "pshufh $f8, $f8, $f20          \r\n"
            "pshufh $f10, $f10, $f20        \r\n"
            "punpcklbh $f2, $f2, $f20       \r\n"
            "punpcklbh $f4, $f4, $f20       \r\n"
            "pmullh $f2, $f2, $f6           \r\n"
            "pmullh $f4, $f4, $f8           \r\n"
            "paddsh $f2, $f2, $f10          \r\n"
            "paddsh $f2, $f2, $f4           \r\n"
            "psrah $f2, $f2, $f12           \r\n"
            "packushb $f2, $f2, $f20        \r\n"
            "swc1 $f2, %0                   \r\n"
            : "=m"(*dst)
            : "m"(*src),"m"(*dst),"r"(weights),
              "r"(weightd),"r"(offset),"r"(log2_denom+1)
        );
    }
}
