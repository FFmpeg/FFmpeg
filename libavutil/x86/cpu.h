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

#ifndef AVUTIL_X86_CPU_H
#define AVUTIL_X86_CPU_H

#include "config.h"
#include "libavutil/cpu.h"

#define CPUEXT(flags, suffix, cpuext)                                   \
    (HAVE_ ## cpuext ## suffix && ((flags) & AV_CPU_FLAG_ ## cpuext))

#define AV_CPU_FLAG_AMD3DNOW    AV_CPU_FLAG_3DNOW
#define AV_CPU_FLAG_AMD3DNOWEXT AV_CPU_FLAG_3DNOWEXT

#define EXTERNAL_AMD3DNOW(flags)    CPUEXT(flags, _EXTERNAL, AMD3DNOW)
#define EXTERNAL_AMD3DNOWEXT(flags) CPUEXT(flags, _EXTERNAL, AMD3DNOWEXT)
#define EXTERNAL_MMX(flags)         CPUEXT(flags, _EXTERNAL, MMX)
#define EXTERNAL_MMXEXT(flags)      CPUEXT(flags, _EXTERNAL, MMXEXT)
#define EXTERNAL_SSE(flags)         CPUEXT(flags, _EXTERNAL, SSE)
#define EXTERNAL_SSE2(flags)        CPUEXT(flags, _EXTERNAL, SSE2)
#define EXTERNAL_SSE3(flags)        CPUEXT(flags, _EXTERNAL, SSE3)
#define EXTERNAL_SSSE3(flags)       CPUEXT(flags, _EXTERNAL, SSSE3)
#define EXTERNAL_SSE4(flags)        CPUEXT(flags, _EXTERNAL, SSE4)
#define EXTERNAL_SSE42(flags)       CPUEXT(flags, _EXTERNAL, SSE42)
#define EXTERNAL_AVX(flags)         CPUEXT(flags, _EXTERNAL, AVX)
#define EXTERNAL_FMA4(flags)        CPUEXT(flags, _EXTERNAL, FMA4)

#define INLINE_AMD3DNOW(flags)      CPUEXT(flags, _INLINE, AMD3DNOW)
#define INLINE_AMD3DNOWEXT(flags)   CPUEXT(flags, _INLINE, AMD3DNOWEXT)
#define INLINE_MMX(flags)           CPUEXT(flags, _INLINE, MMX)
#define INLINE_MMXEXT(flags)        CPUEXT(flags, _INLINE, MMXEXT)
#define INLINE_SSE(flags)           CPUEXT(flags, _INLINE, SSE)
#define INLINE_SSE2(flags)          CPUEXT(flags, _INLINE, SSE2)
#define INLINE_SSE3(flags)          CPUEXT(flags, _INLINE, SSE3)
#define INLINE_SSSE3(flags)         CPUEXT(flags, _INLINE, SSSE3)
#define INLINE_SSE4(flags)          CPUEXT(flags, _INLINE, SSE4)
#define INLINE_SSE42(flags)         CPUEXT(flags, _INLINE, SSE42)
#define INLINE_AVX(flags)           CPUEXT(flags, _INLINE, AVX)
#define INLINE_FMA4(flags)          CPUEXT(flags, _INLINE, FMA4)

void ff_cpu_cpuid(int index, int *eax, int *ebx, int *ecx, int *edx);
void ff_cpu_xgetbv(int op, int *eax, int *edx);
int  ff_cpu_cpuid_test(void);

#endif /* AVUTIL_X86_CPU_H */
