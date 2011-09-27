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

static int flags, checked;

void av_force_cpu_flags(int arg){
    flags   = arg;
    checked = 1;
}

int av_get_cpu_flags(void)
{
    if (checked)
        return flags;

    if (ARCH_ARM) flags = ff_get_cpu_flags_arm();
    if (ARCH_PPC) flags = ff_get_cpu_flags_ppc();
    if (ARCH_X86) flags = ff_get_cpu_flags_x86();

    checked = 1;
    return flags;
}

#ifdef TEST

#undef printf
#include <stdio.h>

static const struct {
    int flag;
    const char *name;
} cpu_flag_tab[] = {
#if   ARCH_ARM
    { AV_CPU_FLAG_IWMMXT,    "iwmmxt"     },
#elif ARCH_PPC
    { AV_CPU_FLAG_ALTIVEC,   "altivec"    },
#elif ARCH_X86
    { AV_CPU_FLAG_MMX,       "mmx"        },
    { AV_CPU_FLAG_MMX2,      "mmx2"       },
    { AV_CPU_FLAG_SSE,       "sse"        },
    { AV_CPU_FLAG_SSE2,      "sse2"       },
    { AV_CPU_FLAG_SSE2SLOW,  "sse2(slow)" },
    { AV_CPU_FLAG_SSE3,      "sse3"       },
    { AV_CPU_FLAG_SSE3SLOW,  "sse3(slow)" },
    { AV_CPU_FLAG_SSSE3,     "ssse3"      },
    { AV_CPU_FLAG_ATOM,      "atom"       },
    { AV_CPU_FLAG_SSE4,      "sse4.1"     },
    { AV_CPU_FLAG_SSE42,     "sse4.2"     },
    { AV_CPU_FLAG_AVX,       "avx"        },
    { AV_CPU_FLAG_XOP,       "xop"        },
    { AV_CPU_FLAG_FMA4,      "fma4"       },
    { AV_CPU_FLAG_3DNOW,     "3dnow"      },
    { AV_CPU_FLAG_3DNOWEXT,  "3dnowext"   },
#endif
    { 0 }
};

int main(void)
{
    int cpu_flags = av_get_cpu_flags();
    int i;

    printf("cpu_flags = 0x%08X\n", cpu_flags);
    printf("cpu_flags =");
    for (i = 0; cpu_flag_tab[i].flag; i++)
        if (cpu_flags & cpu_flag_tab[i].flag)
            printf(" %s", cpu_flag_tab[i].name);
    printf("\n");

    return 0;
}

#endif
