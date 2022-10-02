/*
 * Copyright © 2022 Rémi Denis-Courmont.
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

#include "libavutil/cpu.h"
#include "libavutil/cpu_internal.h"
#include "libavutil/log.h"
#include "config.h"

#if HAVE_GETAUXVAL
#include <sys/auxv.h>
#define HWCAP_RV(letter) (1ul << ((letter) - 'A'))
#endif

int ff_get_cpu_flags_riscv(void)
{
    int ret = 0;
#if HAVE_GETAUXVAL
    const unsigned long hwcap = getauxval(AT_HWCAP);

    if (hwcap & HWCAP_RV('I'))
        ret |= AV_CPU_FLAG_RVI;
    if (hwcap & HWCAP_RV('F'))
        ret |= AV_CPU_FLAG_RVF;
    if (hwcap & HWCAP_RV('D'))
        ret |= AV_CPU_FLAG_RVD;
    if (hwcap & HWCAP_RV('B'))
        ret |= AV_CPU_FLAG_RVB_BASIC;

    /* The V extension implies all Zve* functional subsets */
    if (hwcap & HWCAP_RV('V'))
        ret |= AV_CPU_FLAG_RVV_I32 | AV_CPU_FLAG_RVV_I64
             | AV_CPU_FLAG_RVV_F32 | AV_CPU_FLAG_RVV_F64;
#endif

#ifdef __riscv_i
    ret |= AV_CPU_FLAG_RVI;
#endif
#if defined (__riscv_flen) && (__riscv_flen >= 32)
    ret |= AV_CPU_FLAG_RVF;
#if (__riscv_flen >= 64)
    ret |= AV_CPU_FLAG_RVD;
#endif
#endif

#ifdef __riscv_zbb
    ret |= AV_CPU_FLAG_RVB_BASIC;
#endif

    /* If RV-V is enabled statically at compile-time, check the details. */
#ifdef __riscv_vectors
    ret |= AV_CPU_FLAG_RVV_I32;
#if __riscv_v_elen >= 64
    ret |= AV_CPU_FLAG_RVV_I64;
#endif
#if __riscv_v_elen_fp >= 32
    ret |= AV_CPU_FLAG_RVV_F32;
#if __riscv_v_elen_fp >= 64
    ret |= AV_CPU_FLAG_RVV_F64;
#endif
#endif
#endif

    return ret;
}
