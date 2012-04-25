/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "cpu.h"
#include "config.h"
#include "opt.h"

static int cpuflags_mask = -1, checked;

int av_get_cpu_flags(void)
{
    static int flags;

    if (checked)
        return flags;

    if (ARCH_ARM) flags = ff_get_cpu_flags_arm();
    if (ARCH_PPC) flags = ff_get_cpu_flags_ppc();
    if (ARCH_X86) flags = ff_get_cpu_flags_x86();

    flags  &= cpuflags_mask;
    checked = 1;

    return flags;
}

void av_set_cpu_flags_mask(int mask)
{
    cpuflags_mask = mask;
    checked       = 0;
}

int av_parse_cpu_flags(const char *s)
{
#define CPUFLAG_MMX2     (AV_CPU_FLAG_MMX      | AV_CPU_FLAG_MMX2)
#define CPUFLAG_3DNOW    (AV_CPU_FLAG_3DNOW    | AV_CPU_FLAG_MMX)
#define CPUFLAG_3DNOWEXT (AV_CPU_FLAG_3DNOWEXT | CPUFLAG_3DNOW)
#define CPUFLAG_SSE      (AV_CPU_FLAG_SSE      | CPUFLAG_MMX2)
#define CPUFLAG_SSE2     (AV_CPU_FLAG_SSE2     | CPUFLAG_SSE)
#define CPUFLAG_SSE2SLOW (AV_CPU_FLAG_SSE2SLOW | CPUFLAG_SSE2)
#define CPUFLAG_SSE3     (AV_CPU_FLAG_SSE3     | CPUFLAG_SSE2)
#define CPUFLAG_SSE3SLOW (AV_CPU_FLAG_SSE3SLOW | CPUFLAG_SSE3)
#define CPUFLAG_SSSE3    (AV_CPU_FLAG_SSSE3    | CPUFLAG_SSE3)
#define CPUFLAG_SSE4     (AV_CPU_FLAG_SSE4     | CPUFLAG_SSSE3)
#define CPUFLAG_SSE42    (AV_CPU_FLAG_SSE42    | CPUFLAG_SSE4)
#define CPUFLAG_AVX      (AV_CPU_FLAG_AVX      | CPUFLAG_SSE42)
#define CPUFLAG_XOP      (AV_CPU_FLAG_XOP      | CPUFLAG_AVX)
#define CPUFLAG_FMA4     (AV_CPU_FLAG_FMA4     | CPUFLAG_AVX)
    static const AVOption cpuflags_opts[] = {
        { "flags"   , NULL, 0, AV_OPT_TYPE_FLAGS, { 0 }, INT64_MIN, INT64_MAX, .unit = "flags" },
#if   ARCH_PPC
        { "altivec" , NULL, 0, AV_OPT_TYPE_CONST, { AV_CPU_FLAG_ALTIVEC  },    .unit = "flags" },
#elif ARCH_X86
        { "mmx"     , NULL, 0, AV_OPT_TYPE_CONST, { AV_CPU_FLAG_MMX      },    .unit = "flags" },
        { "mmx2"    , NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_MMX2         },    .unit = "flags" },
        { "sse"     , NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_SSE          },    .unit = "flags" },
        { "sse2"    , NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_SSE2         },    .unit = "flags" },
        { "sse2slow", NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_SSE2SLOW     },    .unit = "flags" },
        { "sse3"    , NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_SSE3         },    .unit = "flags" },
        { "sse3slow", NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_SSE3SLOW     },    .unit = "flags" },
        { "ssse3"   , NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_SSSE3        },    .unit = "flags" },
        { "atom"    , NULL, 0, AV_OPT_TYPE_CONST, { AV_CPU_FLAG_ATOM     },    .unit = "flags" },
        { "sse4.1"  , NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_SSE4         },    .unit = "flags" },
        { "sse4.2"  , NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_SSE42        },    .unit = "flags" },
        { "avx"     , NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_AVX          },    .unit = "flags" },
        { "xop"     , NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_XOP          },    .unit = "flags" },
        { "fma4"    , NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_FMA4         },    .unit = "flags" },
        { "3dnow"   , NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_3DNOW        },    .unit = "flags" },
        { "3dnowext", NULL, 0, AV_OPT_TYPE_CONST, { CPUFLAG_3DNOWEXT     },    .unit = "flags" },
#elif ARCH_ARM
        { "armv5te",  NULL, 0, AV_OPT_TYPE_CONST, { AV_CPU_FLAG_ARMV5TE  },    .unit = "flags" },
        { "armv6",    NULL, 0, AV_OPT_TYPE_CONST, { AV_CPU_FLAG_ARMV6    },    .unit = "flags" },
        { "armv6t2",  NULL, 0, AV_OPT_TYPE_CONST, { AV_CPU_FLAG_ARMV6T2  },    .unit = "flags" },
        { "vfp",      NULL, 0, AV_OPT_TYPE_CONST, { AV_CPU_FLAG_VFP      },    .unit = "flags" },
        { "vfpv3",    NULL, 0, AV_OPT_TYPE_CONST, { AV_CPU_FLAG_VFPV3    },    .unit = "flags" },
        { "neon",     NULL, 0, AV_OPT_TYPE_CONST, { AV_CPU_FLAG_NEON     },    .unit = "flags" },
#endif
        { NULL },
    };
    static const AVClass class = {
        .class_name = "cpuflags",
        .item_name  = av_default_item_name,
        .option     = cpuflags_opts,
        .version    = LIBAVUTIL_VERSION_INT,
    };

    int flags = 0, ret;
    const AVClass *pclass = &class;

    if ((ret = av_opt_eval_flags(&pclass, &cpuflags_opts[0], s, &flags)) < 0)
        return ret;

    return flags & INT_MAX;
}

#ifdef TEST

#undef printf
#include <stdio.h>

static const struct {
    int flag;
    const char *name;
} cpu_flag_tab[] = {
#if   ARCH_ARM
    { AV_CPU_FLAG_ARMV5TE,   "armv5te"    },
    { AV_CPU_FLAG_ARMV6,     "armv6"      },
    { AV_CPU_FLAG_ARMV6T2,   "armv6t2"    },
    { AV_CPU_FLAG_VFP,       "vfp"        },
    { AV_CPU_FLAG_VFPV3,     "vfpv3"      },
    { AV_CPU_FLAG_NEON,      "neon"       },
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
