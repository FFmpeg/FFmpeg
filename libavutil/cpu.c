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

#include "cpu.h"
#include "config.h"

#if   ARCH_ARM
#   include "arm/cpu.h"
#elif ARCH_PPC
#   include "ppc/cpu.h"
#elif ARCH_X86
#   include "x86/cpu.h"
#else
int av_get_cpu_flags(void)
{
    return 0;
}
#endif

#ifdef TEST

#undef printf

int main(void)
{
    int cpu_flags = av_get_cpu_flags();

    printf("cpu_flags = 0x%08X\n", cpu_flags);
    printf("cpu_flags = %s%s%s%s%s%s%s%s%s%s%s%s\n",
#if   ARCH_ARM
           cpu_flags & AV_CPU_FLAG_IWMMXT   ? "IWMMXT "     : "",
#elif ARCH_PPC
           cpu_flags & AV_CPU_FLAG_ALTIVEC  ? "ALTIVEC "    : "",
#elif ARCH_X86
           cpu_flags & AV_CPU_FLAG_MMX      ? "MMX "        : "",
           cpu_flags & AV_CPU_FLAG_MMX2     ? "MMX2 "       : "",
           cpu_flags & AV_CPU_FLAG_SSE      ? "SSE "        : "",
           cpu_flags & AV_CPU_FLAG_SSE2     ? "SSE2 "       : "",
           cpu_flags & AV_CPU_FLAG_SSE2SLOW ? "SSE2(slow) " : "",
           cpu_flags & AV_CPU_FLAG_SSE3     ? "SSE3 "       : "",
           cpu_flags & AV_CPU_FLAG_SSE3SLOW ? "SSE3(slow) " : "",
           cpu_flags & AV_CPU_FLAG_SSSE3    ? "SSSE3 "      : "",
           cpu_flags & AV_CPU_FLAG_SSE4     ? "SSE4.1 "     : "",
           cpu_flags & AV_CPU_FLAG_SSE42    ? "SSE4.2 "     : "",
           cpu_flags & AV_CPU_FLAG_3DNOW    ? "3DNow "      : "",
           cpu_flags & AV_CPU_FLAG_3DNOWEXT ? "3DNowExt "   : "");
#endif
    return 0;
}

#endif
