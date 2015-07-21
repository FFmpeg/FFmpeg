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

void ff_put_pixels_clamped_mmi(const int16_t *block,
        uint8_t *av_restrict pixels, ptrdiff_t line_size)
{
    const int16_t *p;
    uint8_t *pix;

    p = block;
    pix = pixels;

    __asm__ volatile (
        "ldc1 $f0, 0+%3                 \r\n"
        "ldc1 $f2, 8+%3                 \r\n"
        "ldc1 $f4, 16+%3                \r\n"
        "ldc1 $f6, 24+%3                \r\n"
        "ldc1 $f8, 32+%3                \r\n"
        "ldc1 $f10, 40+%3               \r\n"
        "ldc1 $f12, 48+%3               \r\n"
        "ldc1 $f14, 56+%3               \r\n"
        "dadd $10, %0, %1               \r\n"
        "packushb $f0, $f0, $f2         \r\n"
        "packushb $f4, $f4, $f6         \r\n"
        "packushb $f8, $f8, $f10        \r\n"
        "packushb $f12, $f12, $f14      \r\n"
        "sdc1 $f0, 0(%0)                \r\n"
        "sdc1 $f4, 0($10)               \r\n"
        "gssdxc1 $f8, 0($10, %1)        \r\n"
        "gssdxc1 $f12, 0(%0, %2)        \r\n"
        ::"r"(pix),"r"((int)line_size),
          "r"((int)line_size*3),"m"(*p)
        : "$10","memory"
    );

    pix += line_size*4;
    p += 32;

    __asm__ volatile (
        "ldc1 $f0, 0+%3                 \r\n"
        "ldc1 $f2, 8+%3                 \r\n"
        "ldc1 $f4, 16+%3                \r\n"
        "ldc1 $f6, 24+%3                \r\n"
        "ldc1 $f8, 32+%3                \r\n"
        "ldc1 $f10, 40+%3               \r\n"
        "ldc1 $f12, 48+%3               \r\n"
        "ldc1 $f14, 56+%3               \r\n"
        "dadd $10, %0, %1               \r\n"
        "packushb $f0, $f0, $f2         \r\n"
        "packushb $f4, $f4, $f6         \r\n"
        "packushb $f8, $f8, $f10        \r\n"
        "packushb $f12, $f12, $f14      \r\n"
        "sdc1 $f0, 0(%0)                \r\n"
        "sdc1 $f4, 0($10)               \r\n"
        "gssdxc1 $f8, 0($10, %1)        \r\n"
        "gssdxc1 $f12, 0(%0, %2)        \r\n"
        ::"r"(pix),"r"((int)line_size),
          "r"((int)line_size*3),"m"(*p)
        : "$10","memory"
    );
}

void ff_put_signed_pixels_clamped_mmi(const int16_t *block,
    uint8_t *av_restrict pixels, ptrdiff_t line_size)
{
    int64_t line_skip = line_size;
    int64_t line_skip3;

    __asm__ volatile (
        "dmtc1 %4, $f0                  \n\t"
        "daddu %1, %3, %3               \n\t"
        "ldc1 $f2, 0(%2)                \n\t"
        "ldc1 $f10, 8(%2)               \n\t"
        "packsshb $f2, $f2, $f10        \n\t"
        "ldc1 $f4, 16(%2)               \n\t"
        "ldc1 $f10, 24(%2)              \n\t"
        "packsshb $f4, $f4, $f10        \n\t"
        "ldc1 $f6, 32(%2)               \n\t"
        "ldc1 $f10, 40(%2)              \n\t"
        "packsshb $f6, $f6, $f10        \n\t"
        "ldc1 $f8, 48(%2)               \n\t"
        "ldc1 $f10, 56(%2)              \n\t"
        "packsshb $f8, $f8, $f10        \n\t"
        "paddb $f2, $f2, $f0            \n\t"
        "paddb $f4, $f4, $f0            \n\t"
        "paddb $f6, $f6, $f0            \n\t"
        "paddb $f8, $f8, $f0            \n\t"
        "sdc1 $f2, 0(%0)                \n\t"
        "gssdxc1 $f4, 0(%0, %3)         \n\t"
        "gssdxc1 $f6, 0(%0, %1)         \n\t"
        "daddu %1, %1, %3               \n\t"
        "gssdxc1 $f8, 0(%0, %1)         \n\t"
        "daddu $10, %1, %3              \n\t"
        "daddu %0, %0, $10              \n\t"
        "ldc1 $f2, 64(%2)               \n\t"
        "ldc1 $f10, 8+64(%2)            \n\t"
        "packsshb  $f2, $f2, $f10       \n\t"
        "ldc1 $f4, 16+64(%2)            \n\t"
        "ldc1 $f10, 24+64(%2)           \n\t"
        "packsshb $f4, $f4, $f10        \n\t"
        "ldc1 $f6, 32+64(%2)            \n\t"
        "ldc1 $f10, 40+64(%2)           \n\t"
        "packsshb $f6, $f6, $f10        \n\t"
        "ldc1 $f8, 48+64(%2)            \n\t"
        "ldc1 $f10, 56+64(%2)           \n\t"
        "packsshb $f8, $f8, $f10        \n\t"
        "paddb $f2, $f2, $f0            \n\t"
        "paddb $f4, $f4, $f0            \n\t"
        "paddb $f6, $f6, $f0            \n\t"
        "paddb $f8, $f8, $f0            \n\t"
        "sdc1 $f2, 0(%0)                \n\t"
        "gssdxc1 $f4, 0(%0, %3)         \n\t"
        "daddu $10, %3, %3              \n\t"
        "gssdxc1 $f6, 0(%0, $10)        \n\t"
        "gssdxc1 $f8, 0(%0, %1)         \n\t"
        : "+&r"(pixels),"=&r"(line_skip3)
        : "r"(block),"r"(line_skip),"r"(ff_pb_80)
        : "$10","memory"
    );
}

void ff_add_pixels_clamped_mmi(const int16_t *block,
        uint8_t *av_restrict pixels, ptrdiff_t line_size)
{
    const int16_t *p;
    uint8_t *pix;
    int i = 4;

    p = block;
    pix = pixels;

    __asm__ volatile (
        "xor $f14, $f14, $f14           \r\n"
        ::
    );

    do {
        __asm__ volatile (
            "ldc1 $f0, 0+%2             \r\n"
            "ldc1 $f2, 8+%2             \r\n"
            "ldc1 $f4, 16+%2            \r\n"
            "ldc1 $f6, 24+%2            \r\n"
            "ldc1 $f8, %0               \r\n"
            "ldc1 $f12, %1              \r\n"
            "mov.d $f10, $f8            \r\n"
            "punpcklbh $f8, $f8, $f14   \r\n"
            "punpckhbh $f10, $f10, $f14 \r\n"
            "paddsh $f0, $f0, $f8       \r\n"
            "paddsh $f2, $f2, $f10      \r\n"
            "mov.d $f10, $f12           \r\n"
            "punpcklbh $f12, $f12, $f14 \r\n"
            "punpckhbh $f10, $f10, $f14 \r\n"
            "paddsh $f4, $f4, $f12      \r\n"
            "paddsh $f6, $f6, $f10      \r\n"
            "packushb $f0, $f0, $f2     \r\n"
            "packushb $f4, $f4, $f6     \r\n"
            "sdc1 $f0, %0               \r\n"
            "sdc1 $f4, %1               \r\n"
            : "+m"(*pix),"+m"(*(pix+line_size))
            : "m"(*p)
            : "memory"
        );

        pix += line_size*2;
        p += 16;
    } while (--i);
}
