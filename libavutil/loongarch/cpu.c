/*
 * Copyright (c) 2020 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
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

#include <stdint.h>
#include "cpu.h"

#define LOONGARCH_CFG2 0x2
#define LOONGARCH_CFG2_LSX    (1 << 6)
#define LOONGARCH_CFG2_LASX   (1 << 7)

static int cpu_flags_cpucfg(void)
{
    int flags = 0;
    uint32_t cfg2 = 0;

    __asm__ volatile(
        "cpucfg %0, %1 \n\t"
        : "+&r"(cfg2)
        : "r"(LOONGARCH_CFG2)
    );

    if (cfg2 & LOONGARCH_CFG2_LSX)
        flags |= AV_CPU_FLAG_LSX;

    if (cfg2 & LOONGARCH_CFG2_LASX)
        flags |= AV_CPU_FLAG_LASX;

    return flags;
}

int ff_get_cpu_flags_loongarch(void)
{
#if defined __linux__
    return cpu_flags_cpucfg();
#else
    /* Assume no SIMD ASE supported */
    return 0;
#endif
}

size_t ff_get_cpu_max_align_loongarch(void)
{
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_LASX)
        return 32;
    if (flags & AV_CPU_FLAG_LSX)
        return 16;

    return 8;
}
