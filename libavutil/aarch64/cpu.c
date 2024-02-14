/*
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
#include "config.h"

#if (defined(__linux__) || defined(__ANDROID__)) && HAVE_GETAUXVAL
#include <stdint.h>
#include <sys/auxv.h>

#define HWCAP_AARCH64_ASIMDDP (1 << 20)
#define HWCAP2_AARCH64_I8MM   (1 << 13)

static int detect_flags(void)
{
    int flags = 0;

    unsigned long hwcap = getauxval(AT_HWCAP);
    unsigned long hwcap2 = getauxval(AT_HWCAP2);

    if (hwcap & HWCAP_AARCH64_ASIMDDP)
        flags |= AV_CPU_FLAG_DOTPROD;
    if (hwcap2 & HWCAP2_AARCH64_I8MM)
        flags |= AV_CPU_FLAG_I8MM;

    return flags;
}

#elif defined(__APPLE__) && HAVE_SYSCTLBYNAME
#include <sys/sysctl.h>

static int detect_flags(void)
{
    uint32_t value = 0;
    size_t size;
    int flags = 0;

    size = sizeof(value);
    if (!sysctlbyname("hw.optional.arm.FEAT_DotProd", &value, &size, NULL, 0)) {
        if (value)
            flags |= AV_CPU_FLAG_DOTPROD;
    }
    size = sizeof(value);
    if (!sysctlbyname("hw.optional.arm.FEAT_I8MM", &value, &size, NULL, 0)) {
        if (value)
            flags |= AV_CPU_FLAG_I8MM;
    }
    return flags;
}

#elif defined(_WIN32)
#include <windows.h>

static int detect_flags(void)
{
    int flags = 0;
#ifdef PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE
    if (IsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE))
        flags |= AV_CPU_FLAG_DOTPROD;
#endif
    return flags;
}
#else

static int detect_flags(void)
{
    return 0;
}

#endif

int ff_get_cpu_flags_aarch64(void)
{
    int flags = AV_CPU_FLAG_ARMV8 * HAVE_ARMV8 |
                AV_CPU_FLAG_NEON  * HAVE_NEON;

#ifdef __ARM_FEATURE_DOTPROD
    flags |= AV_CPU_FLAG_DOTPROD;
#endif
#ifdef __ARM_FEATURE_MATMUL_INT8
    flags |= AV_CPU_FLAG_I8MM;
#endif

    flags |= detect_flags();

    return flags;
}

size_t ff_get_cpu_max_align_aarch64(void)
{
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_NEON)
        return 16;

    return 8;
}
